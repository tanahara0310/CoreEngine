#include "pch.h"
#include "RTShadowPass.h"

#include "EngineSystem/Subsystem/RayTracingSubsystem.h"
#include <format>

#include "Graphics/Light/LightManager.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RayTracing/Debug/RTDebugRegistry.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/RayTracing/RayTracingShadowManager.h"
#include "Graphics/Render/Pass/RTWaterCausticsPass.h"
#include "Graphics/Render/RenderGraph.h"
#include "Graphics/Water/RayTracing/WaterCausticsRayTracingManager.h"
#include "Graphics/Water/WaterSurfaceData.h"

namespace CoreEngine
{
    namespace {
        constexpr RayTracingShadowManager::ViewID kShadowView = RayTracingShadowManager::ViewID::GameView;

        /// @brief RT シャドウ各ステージ共通の実行前提を検証してコマンドリストを返す
        ID3D12GraphicsCommandList* ResolveRTShadowCommandList(const RenderContext& context)
        {
            if (!context.rayTracingSubsystem || !context.dxCommon || !context.rtShadowManager) {
                return nullptr;
            }
            if (!context.rtShadowManager->IsInitialized()) {
                return nullptr;
            }
            return context.cmdList;
        }

        /// @brief 水中の受光点を屈折した経路で調べるための水面を作る
        /// @details RT コースティクスが水中の直接光を置き換えるときだけ有効にする
        ///          （コースティクスが水中の区間の遮蔽を、影が水より上の区間の遮蔽を受け持つ）
        RayTracingShadowWaterSurface BuildShadowWaterSurface(const RenderContext& context)
        {
            RayTracingShadowWaterSurface water{};
            if (!IsRayTracedWaterCausticsSelected(context)
                || !context.rtWaterCausticsManager
                || !context.rtWaterCausticsManager->IsInitialized()) {
                return water;
            }

            const WaterSurfaceData& surface = *context.waterSurfaceState;
            water.enabled = true;
            water.height = surface.waterHeight;
            water.refractiveIndex = context.rtWaterCausticsManager->GetSettings().refractiveIndex;
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
            RayTracingShadowManager* shadow = query.domain ? query.domain->GetRayTracingShadowManager() : nullptr;
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
            RayTracingShadowManager* shadow = context.rtShadowManager;
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

    void RTShadowPass::DeclareResources(RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        // GBuffer を読み取り、ライトごとのレイトレース結果を生成する。
        builder.Read(FrameBlackboard::SceneDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Read(FrameBlackboard::GBufferNormalRoughness, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Read(FrameBlackboard::GBufferMotionVector, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Write(FrameBlackboard::RTShadowMask, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    void RTShadowPass::Execute(const RenderContext& context)
    {
        // 止めたフレームは撃たない（後の 2 段も撃っていないライトを飛ばし、ライティングは影なしで照らす）
        if (!kShadowEffect.IsActive()) {
            return;
        }
        ID3D12GraphicsCommandList* cmdList = ResolveRTShadowCommandList(context);
        if (!cmdList) {
            return;
        }

        context.rayTracingSubsystem->DispatchRTShadowTrace(
            context, context.dxCommon, cmdList, kShadowView, BuildShadowWaterSurface(context));
    }

    void RTShadowTemporalPass::DeclareResources(RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        // レイトレース結果（RTShadowMask）へ再投影と Variance Clamping を適用する。
        builder.Read(FrameBlackboard::SceneDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Read(FrameBlackboard::GBufferNormalRoughness, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Read(FrameBlackboard::GBufferMotionVector, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Write(FrameBlackboard::RTShadowMask, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    void RTShadowTemporalPass::Execute(const RenderContext& context)
    {
        ID3D12GraphicsCommandList* cmdList = ResolveRTShadowCommandList(context);
        if (!cmdList) {
            return;
        }

        context.rayTracingSubsystem->DispatchRTShadowTemporal(
            context, context.dxCommon, cmdList, kShadowView);
    }

    void RTShadowDenoisePass::DeclareResources(RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        // テンポラル蓄積済みの RTShadowMask へ A-Trous デノイズを適用する。
        builder.Read(FrameBlackboard::SceneDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Read(FrameBlackboard::GBufferNormalRoughness, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Write(FrameBlackboard::RTShadowMask, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    void RTShadowDenoisePass::Execute(const RenderContext& context)
    {
        ID3D12GraphicsCommandList* cmdList = ResolveRTShadowCommandList(context);
        if (!cmdList) {
            return;
        }

        context.rayTracingSubsystem->DispatchRTShadowDenoise(
            context, context.dxCommon, cmdList, kShadowView);

        // RTShadowMask の Blackboard 登録は RegisterFrameResources が行うため、
        // 実行中の再登録は不要（パス分離契約 3）。
    }
}
