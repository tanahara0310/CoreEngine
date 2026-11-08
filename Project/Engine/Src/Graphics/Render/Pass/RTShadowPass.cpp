#include "pch.h"
#include "RTShadowPass.h"

#include <format>
#include <functional>

#include "Camera/View/ViewInfo.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RayTracing/Debug/RTDebugRegistry.h"
#include "Graphics/RayTracing/Effect/RTEffectRegistry.h"
#include "Graphics/RayTracing/Effect/RTEffectSet.h"
#include "Graphics/RayTracing/RTScene.h"
#include "Graphics/Render/FrameBlackboard.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/RayTracing/RayTracingShadowManager.h"
#include "Graphics/Render/Pass/RTWaterCausticsPass.h"
#include "Graphics/Render/RenderGraph.h"
#include "Graphics/Water/RayTracing/WaterCausticsRayTracingManager.h"
#include "Graphics/Water/WaterSurfaceData.h"

namespace CoreEngine
{
    RT_EFFECT_REGISTER(RayTracingShadowManager,
        .id = "RTShadow",
        .group = nullptr,
        .passes = {
            RTEffectPassDesc::Of<RTShadowPass>(RenderPassPhase::PreLighting, 10),
            RTEffectPassDesc::Of<RTShadowTemporalPass>(RenderPassPhase::PreLighting, 11),
            RTEffectPassDesc::Of<RTShadowDenoisePass>(RenderPassPhase::PreLighting, 12),
        })

    namespace {
        constexpr RayTracingShadowManager::ViewID kShadowView = RayTracingShadowManager::ViewID::GameView;

        /// @brief 影の 3 段で共通の入力一式
        struct ShadowStageInput {
            RayTracingShadowManager* rtShadow = nullptr;
            ID3D12GraphicsCommandList* cmdList = nullptr;
            D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSRV{};
            D3D12_GPU_DESCRIPTOR_HANDLE normalSRV{};
            D3D12_GPU_DESCRIPTOR_HANDLE motionVectorSRV{};
            Matrix4x4 projection{};   ///< 深度線形化用（Temporal / Denoise）
            Matrix4x4 invViewProj{};  ///< ワールド座標復元用（RayGen のみ）
            UINT width = 0;
            UINT height = 0;
        };

        /// @brief 影の各段の共通の前処理
        /// @return 前提が揃ったか。false のときは撃ってはいけない。
        bool BuildShadowStageInput(const RenderContext& context, ShadowStageInput& out)
        {
            RayTracingShadowManager* rtShadow = FindRTEffect<RayTracingShadowManager>(context);
            if (!rtShadow || !rtShadow->IsInitialized()) return false;
            if (!context.gBufferManager || !context.lightManager || !context.sceneManager) return false;
            if (!context.dxCommon || !context.cmdList) return false;

            // 実行中のビューの ViewInfo を使う
            if (!context.frameViews) return false;
            const ViewInfo& view = context.frameViews->Get(context.viewSettings.viewType);
            if (!view.isValid) return false;

            out.rtShadow = rtShadow;
            out.cmdList = context.cmdList;

            // ワールド座標は深度から復元する（FrameBlackboard 経由）
            if (context.frameBlackboard) {
                context.frameBlackboard->TryGetSrvHandle(FrameBlackboard::SceneDepth, out.sceneDepthSRV);
            }
            out.projection = view.projection;
            out.invViewProj = view.invViewProjection;
            out.normalSRV = context.gBufferManager->GetSRVHandle(GBufferManager::Target::NormalRoughness);
            out.motionVectorSRV = context.gBufferManager->GetSRVHandle(GBufferManager::Target::MotionVector);
            out.width = static_cast<UINT>(context.dxCommon->GetClientWidth());
            out.height = static_cast<UINT>(context.dxCommon->GetClientHeight());
            return true;
        }

        /// @brief 影を落とす有効なディレクショナルライトを列挙する
        void ForEachShadowCastingLight(
            const RenderContext& context,
            const std::function<void(uint32_t lightIndex, const Light& light)>& body)
        {
            const uint32_t maxLights = RayTracingShadowManager::kMaxDirectionalLights;
            for (uint32_t li = 0; li < LightManager::MAX_DIRECTIONAL_LIGHTS && li < maxLights; ++li) {
                const Light* dirLight = context.lightManager->GetDirectionalLight(li);
                if (!dirLight || !dirLight->enabled) continue;
                body(li, *dirLight);
            }
        }

        /// @brief 水中の受光点を屈折した経路で調べるための水面を作る
        /// @details RT コースティクスが水中の直接光を置き換えるときだけ有効にする
        ///          （コースティクスが水中の区間の遮蔽を、影が水より上の区間の遮蔽を受け持つ）
        RayTracingShadowWaterSurface BuildShadowWaterSurface(const RenderContext& context)
        {
            RayTracingShadowWaterSurface water{};
            const WaterCausticsRayTracingManager* caustics = FindRTEffect<WaterCausticsRayTracingManager>(context);
            if (!IsRayTracedWaterCausticsSelected(context) || !caustics || !caustics->IsInitialized()) {
                return water;
            }

            const WaterSurfaceData& surface = *context.waterSurfaceState;
            water.enabled = true;
            water.height = surface.waterHeight;
            water.refractiveIndex = caustics->GetSettings().refractiveIndex;
            water.regionCenterXZ[0] = surface.regionCenterXZ[0];
            water.regionCenterXZ[1] = surface.regionCenterXZ[1];
            water.regionHalfExtentXZ[0] = surface.regionHalfExtentXZ[0];
            water.regionHalfExtentXZ[1] = surface.regionHalfExtentXZ[1];
            return water;
        }
    }

    RT_DEBUG_EFFECT_REGISTER(kShadowEffect,
        .id = "RTShadow",
        .label = "影",
        .cvarPrefix = "r.RTShadow",
        .passNames = { "RTShadowTrace", "RTShadowTemporal", "RTShadowDenoise" },
        .stageLabels = { "トレース", "時間の蓄積", "デノイズと仕上げ" },
        .collect = [](const RTDebugQuery& query, std::vector<RTDebugEffectRow>& rows) {
            RayTracingShadowManager* shadow = query.domain
                ? query.domain->GetRTEffects().Find<RayTracingShadowManager>() : nullptr;
            if (!shadow || !shadow->IsInitialized()) {
                return;
            }
            for (uint32_t li = 0; li < RayTracingShadowManager::kMaxDirectionalLights; ++li) {
                const RayTracingDispatchInfo& info = shadow->GetDispatchInfo(kShadowView, li);
                if (info.status == RayTracingDispatchStatus::None) {
                    continue;
                }
                const Light* light = query.lights ? query.lights->GetDirectionalLight(li) : nullptr;
                rows.push_back({ light ? std::format("ライト {}：{}", li, light->name) : std::format("ライト {}", li), info });
            }
        });

#ifdef CORE_EDITOR
    namespace
    {
        /// @brief 影の中間結果の種類
        enum class ShadowDebugImage { Raw, History, Mask };

        /// @brief 今フレームに撃ったライトの影の中間結果を引く
        bool ResolveShadowImage(const RenderContext& context, uint32_t light, ShadowDebugImage image, RTDebugSource& out)
        {
            RayTracingShadowManager* shadow = FindRTEffect<RayTracingShadowManager>(context);
            if (!shadow || !shadow->IsInitialized() || light >= RayTracingShadowManager::kMaxDirectionalLights
                || !shadow->IsDispatchedThisFrame(kShadowView, light)) {
                return false;
            }
            switch (image) {
            case ShadowDebugImage::Raw:
                out.resource = &shadow->GetRawShadowResource(kShadowView, light);
                out.srv = shadow->GetRawShadowSRVHandle(kShadowView, light);
                break;
            case ShadowDebugImage::History:
                out.resource = &shadow->GetHistoryResource(kShadowView, light);
                out.srv = shadow->GetHistorySRVHandle(kShadowView, light);
                break;
            default:
                out.resource = &shadow->GetShadowResource(kShadowView, light);
                out.srv = shadow->GetShadowSRVHandle(kShadowView, light);
                break;
            }
            return out.resource->IsValid() && out.srv.ptr != 0;
        }
    }
#endif

    RT_DEBUG_CHANNEL_REGISTER(
        .effect = "影",
        .stage = "トレース",
        .name = "生の値",
        .description = "デノイズ前のレイの結果（トレース解像度）。R はざらつくのが正しい。"
            "G は影の縁だけ多ければ、縁だけレイを増やす仕組みが効いている",
        .components = { "光が届いた割合", "撃ったレイの本数" },
        .rangeMin = 0.0f,
        .rangeMax = 1.0f,
        .viewMode = RTDebugViewMode::Mono,
        .variantCount = RayTracingShadowManager::kMaxDirectionalLights,
        .variantLabel = "ライト",
        .resolve = [](const RenderContext& context, uint32_t variant, RTDebugSource& out) {
            return ResolveShadowImage(context, variant, ShadowDebugImage::Raw, out);
        })

    RT_DEBUG_CHANNEL_REGISTER(
        .effect = "影",
        .stage = "時間の蓄積",
        .name = "履歴",
        .description = "前のフレームと重ねた結果（トレース解像度）。影の縁がざわつくときは B（重ねた数）を見る。"
            "B が小さい所は履歴を毎フレーム捨てている",
        .components = { "光が届いた割合", "分散", "重ねたフレーム数", "カメラからの距離" },
        .rangeMin = 0.0f,
        .rangeMax = 1.0f,
        .viewMode = RTDebugViewMode::Mono,
        .variantCount = RayTracingShadowManager::kMaxDirectionalLights,
        .variantLabel = "ライト",
        .resolve = [](const RenderContext& context, uint32_t variant, RTDebugSource& out) {
            return ResolveShadowImage(context, variant, ShadowDebugImage::History, out);
        })

    RT_DEBUG_CHANNEL_REGISTER(
        .effect = "影",
        .stage = "デノイズと仕上げ",
        .name = "マスク",
        .description = "ライティングが読む最後の影（画面の解像度）。0 = 影・1 = 日向",
        .components = { "光が届いた割合" },
        .rangeMin = 0.0f,
        .rangeMax = 1.0f,
        .viewMode = RTDebugViewMode::Mono,
        .variantCount = RayTracingShadowManager::kMaxDirectionalLights,
        .variantLabel = "ライト",
        .resolve = [](const RenderContext& context, uint32_t variant, RTDebugSource& out) {
            return ResolveShadowImage(context, variant, ShadowDebugImage::Mask, out);
        })

    void DeclareRTShadowMasks(RenderGraphBuilder& builder, const RenderContext& context, bool write)
    {
        static_assert(RayTracingShadowManager::kMaxDirectionalLights <= FrameBlackboard::kRTShadowMaskCount,
            "RT 影のマスクの論理名をライトの数だけ用意する");
        for (uint32_t li = 0; li < RayTracingShadowManager::kMaxDirectionalLights; ++li) {
            // メインライトのマスクはいつも宣言し、補助ライトのマスクは点いているライトの分だけ宣言する
            if (li != 0) {
                const Light* light = (context.lightManager && li < LightManager::MAX_DIRECTIONAL_LIGHTS)
                    ? context.lightManager->GetDirectionalLight(li) : nullptr;
                if (!light || !light->enabled) {
                    continue;
                }
            }
            // マスクはデノイズの最後にピクセルシェーダの読み取りの状態へ移る
            const char* name = FrameBlackboard::RTShadowMaskOf(li);
            if (write) {
                builder.Write(name, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            } else {
                builder.Read(name, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            }
        }
    }

    void RTShadowPass::DeclareResources(RenderGraphBuilder& builder, const RenderContext& context)
    {
        // GBuffer を読み取り、ライトごとのレイトレース結果を生成する。
        builder.Read(FrameBlackboard::SceneDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Read(FrameBlackboard::GBufferNormalRoughness, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Read(FrameBlackboard::GBufferMotionVector, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        DeclareRTShadowMasks(builder, context, true);
    }

    void RTShadowPass::Execute(const RenderContext& context)
    {
        // 止めたフレームは撃たない（後の 2 段も撃っていないライトを飛ばし、ライティングは影なしで照らす）
        if (!kShadowEffect.IsActive()) {
            return;
        }
        ShadowStageInput stage;
        if (!BuildShadowStageInput(context, stage)) {
            return;
        }

        // 葉などアルファで抜ける所は、材質の表でアルファを調べて影を通す
        const RTHitTableIndices hitTables =
            context.rtScene ? context.rtScene->GetHitTableIndices() : RTHitTableIndices{};

        // 全ライト分 DispatchRays を先に実行（GPU パイプラインを詰めるため）
        // 水中の直接光を RT コースティクスが置き換えるのはメインライト（0 番）だけなので、
        // 水面を渡すのも 0 番だけ
        const RayTracingShadowWaterSurface waterSurface = BuildShadowWaterSurface(context);
        ForEachShadowCastingLight(context, [&](uint32_t li, const Light& light) {
            stage.rtShadow->Dispatch(
                stage.cmdList,
                stage.sceneDepthSRV,
                stage.normalSRV,
                light.direction,
                stage.invViewProj,
                stage.width,
                stage.height,
                kShadowView,
                li,
                &light,
                (li == 0) ? waterSurface : RayTracingShadowWaterSurface{},
                hitTables);
            });
    }

    void RTShadowTemporalPass::DeclareResources(RenderGraphBuilder& builder, const RenderContext& context)
    {
        // レイトレース結果（RTShadowMask）へ再投影と Variance Clamping を適用する。
        builder.Read(FrameBlackboard::SceneDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Read(FrameBlackboard::GBufferNormalRoughness, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Read(FrameBlackboard::GBufferMotionVector, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        DeclareRTShadowMasks(builder, context, true);
    }

    void RTShadowTemporalPass::Execute(const RenderContext& context)
    {
        ShadowStageInput stage;
        if (!BuildShadowStageInput(context, stage)) {
            return;
        }

        // 全ライト分 テンポラル蓄積パス（空間前処理+再投影+Variance Clamping）
        ForEachShadowCastingLight(context, [&](uint32_t li, const Light&) {
            stage.rtShadow->ApplyTemporal(
                stage.cmdList,
                stage.normalSRV,
                stage.sceneDepthSRV,
                stage.motionVectorSRV,
                stage.projection,
                kShadowView,
                li);
            });
    }

    void RTShadowDenoisePass::DeclareResources(RenderGraphBuilder& builder, const RenderContext& context)
    {
        // テンポラル蓄積済みの RTShadowMask へ A-Trous デノイズを適用する。
        builder.Read(FrameBlackboard::SceneDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Read(FrameBlackboard::GBufferNormalRoughness, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        DeclareRTShadowMasks(builder, context, true);
    }

    void RTShadowDenoisePass::Execute(const RenderContext& context)
    {
        ShadowStageInput stage;
        if (!BuildShadowStageInput(context, stage)) {
            return;
        }

        // 全ライト分 A-Trous デノイズをまとめて実行
        ForEachShadowCastingLight(context, [&](uint32_t li, const Light&) {
            stage.rtShadow->Denoise(
                stage.cmdList,
                stage.normalSRV,
                stage.sceneDepthSRV,
                stage.projection,
                kShadowView,
                li);
            });

        // RTShadowMask の Blackboard 登録は RegisterFrameResources が行うため、
        // 実行中の再登録は不要（パス分離契約 3）。
    }
}
