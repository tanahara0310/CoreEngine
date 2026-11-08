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
            RTDenoiseGuides guides{};  ///< 深度・法線・モーションベクター・投影行列
            Matrix4x4 invViewProj{};   ///< ワールド座標復元用（RayGen のみ）
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

            // 実行中のビューの深度・法線・モーションベクター・投影行列
            if (!BuildRTDenoiseGuides(context, out.guides)) return false;

            out.rtShadow = rtShadow;
            out.cmdList = context.cmdList;
            // ワールド座標は深度から復元する
            out.invViewProj = context.frameViews->Get(context.viewSettings.viewType).invViewProjection;
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
                if (!dirLight || !dirLight->enabled || !dirLight->castShadows) continue;
                body(li, *dirLight);
            }
        }

        /// @brief 平行光源の影の形（履歴はライトの実体のアドレスで見分ける）
        RTShadowLightShape MakeDirectionalShadowShape(const Light& light)
        {
            RTShadowLightShape shape{};
            shape.type = LightType::Directional;
            shape.direction = light.direction;
            shape.key = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&light));
            return shape;
        }

#ifdef CORE_EDITOR
        /// @brief 影の枠の表示名（平行光源は正準順の番号、局所ライトは種類と GPU の配列の番号）
        std::string ShadowSlotLabel(const RTDebugQuery& query, const RayTracingShadowManager& shadow, uint32_t shadowSlot)
        {
            if (shadowSlot < RayTracingShadowManager::kLocalSlotBegin) {
                const Light* light = query.lights ? query.lights->GetDirectionalLight(shadowSlot) : nullptr;
                return light ? std::format("ライト {}：{}", shadowSlot, light->name) : std::format("ライト {}", shadowSlot);
            }
            const RayTracingShadowManager::LocalShadowSlot& local =
                shadow.GetLocalShadowSlot(shadowSlot - RayTracingShadowManager::kLocalSlotBegin);
            static constexpr const char* kTypeNames[] = { "平行光源", "点光源", "スポット", "エリア" };
            const char* typeName = kTypeNames[static_cast<uint32_t>(local.type) & 3u];
            const Light* light = (query.lights && local.active) ? query.lights->GetLight(local.handle) : nullptr;
            return light ? std::format("{} {}：{}", typeName, local.gpuIndex, light->name)
                : std::format("局所ライトの枠 {}", shadowSlot - RayTracingShadowManager::kLocalSlotBegin);
        }
#endif

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
            for (uint32_t slot = 0; slot < RayTracingShadowManager::kMaxShadowSlots; ++slot) {
                const RayTracingDispatchInfo& info = shadow->GetDispatchInfo(kShadowView, slot);
                if (info.status == RayTracingDispatchStatus::None) {
                    continue;
                }
                rows.push_back({ ShadowSlotLabel(query, *shadow, slot), info });
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
            if (!shadow || !shadow->IsInitialized() || light >= RayTracingShadowManager::kMaxShadowSlots
                || !shadow->IsDispatchedThisFrame(kShadowView, light)) {
                return false;
            }
            RTDenoiser& denoiser = shadow->GetDenoiser();
            const uint32_t channel = RayTracingShadowManager::GetDenoiseChannel(kShadowView, light);
            switch (image) {
            case ShadowDebugImage::Raw:
                out.resource = &denoiser.GetInput(channel);
                out.srv = denoiser.GetInputSRV(channel);
                break;
            case ShadowDebugImage::History:
                out.resource = &denoiser.GetHistory(channel);
                out.srv = denoiser.GetHistorySRV(channel);
                break;
            default:
                out.resource = &denoiser.GetOutput(channel);
                out.srv = denoiser.GetOutputSRV(channel);
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
        .variantCount = RayTracingShadowManager::kMaxShadowSlots,
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
        .variantCount = RayTracingShadowManager::kMaxShadowSlots,
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
        .variantCount = RayTracingShadowManager::kMaxShadowSlots,
        .variantLabel = "ライト",
        .resolve = [](const RenderContext& context, uint32_t variant, RTDebugSource& out) {
            return ResolveShadowImage(context, variant, ShadowDebugImage::Mask, out);
        })

    void DeclareRTShadowMasks(RenderGraphBuilder& builder, const RenderContext& context, bool write)
    {
        static_assert(RayTracingShadowManager::kMaxShadowSlots <= FrameBlackboard::kRTShadowMaskCount,
            "RT 影のマスクの論理名を影の枠の数だけ用意する");
        const auto declare = [&](uint32_t shadowSlot) {
            // マスクはデノイズの最後にピクセルシェーダの読み取りの状態へ移る
            const char* name = FrameBlackboard::RTShadowMaskOf(shadowSlot);
            if (write) {
                builder.Write(name, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            } else {
                builder.Read(name, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            }
        };

        // メインライトのマスクはいつも宣言し、補助ライトのマスクは影を落とす点いているライトの分だけ宣言する
        for (uint32_t li = 0; li < RayTracingShadowManager::kMaxDirectionalLights; ++li) {
            if (li != 0) {
                const Light* light = (context.lightManager && li < LightManager::MAX_DIRECTIONAL_LIGHTS)
                    ? context.lightManager->GetDirectionalLight(li) : nullptr;
                if (!light || !light->enabled || !light->castShadows) {
                    continue;
                }
            }
            declare(li);
        }

        // 局所ライトのマスクは、このフレームに枠へ割り当てたライトの分だけ宣言する
        if (const RayTracingShadowManager* rtShadow = FindRTEffect<RayTracingShadowManager>(context)) {
            for (uint32_t local = 0; local < RayTracingShadowManager::kMaxLocalLights; ++local) {
                if (rtShadow->GetLocalShadowSlot(local).active) {
                    declare(RayTracingShadowManager::kLocalSlotBegin + local);
                }
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
                stage.guides.sceneDepthSRV,
                stage.guides.normalRoughnessSRV,
                MakeDirectionalShadowShape(light),
                stage.invViewProj,
                stage.width,
                stage.height,
                kShadowView,
                li,
                (li == 0) ? waterSurface : RayTracingShadowWaterSurface{},
                hitTables);
            });

        // 局所ライトは、SelectLocalShadowLights が枠へ割り当てたライトだけ撃つ
        for (uint32_t local = 0; local < RayTracingShadowManager::kMaxLocalLights; ++local) {
            const RayTracingShadowManager::LocalShadowSlot& localSlot = stage.rtShadow->GetLocalShadowSlot(local);
            if (!localSlot.active) {
                continue;
            }
            stage.rtShadow->Dispatch(
                stage.cmdList,
                stage.guides.sceneDepthSRV,
                stage.guides.normalRoughnessSRV,
                localSlot.shape,
                stage.invViewProj,
                stage.width,
                stage.height,
                kShadowView,
                RayTracingShadowManager::kLocalSlotBegin + local,
                RayTracingShadowWaterSurface{},
                hitTables);
        }
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

        // 今フレームに撃った影の枠ごとに時間方向の蓄積
        for (uint32_t slot = 0; slot < RayTracingShadowManager::kMaxShadowSlots; ++slot) {
            if (!stage.rtShadow->IsDispatchedThisFrame(kShadowView, slot)) {
                continue;
            }
            stage.rtShadow->ApplyTemporal(stage.cmdList, stage.guides, kShadowView, slot);
        }
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

        // 今フレームに撃った影の枠ごとに A-Trous とマスクへの仕上げ
        for (uint32_t slot = 0; slot < RayTracingShadowManager::kMaxShadowSlots; ++slot) {
            if (!stage.rtShadow->IsDispatchedThisFrame(kShadowView, slot)) {
                continue;
            }
            stage.rtShadow->Denoise(stage.cmdList, stage.guides, kShadowView, slot);
        }

        // RTShadowMask の Blackboard 登録は RegisterFrameResources が行うため、
        // 実行中の再登録は不要（パス分離契約 3）。
    }
}
