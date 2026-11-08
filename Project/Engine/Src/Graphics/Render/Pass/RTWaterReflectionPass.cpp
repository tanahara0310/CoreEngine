#include "pch.h"
#include "RTWaterReflectionPass.h"

#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RayTracing/Debug/RTDebugRegistry.h"
#include "Graphics/RayTracing/Effect/RTEffectRegistry.h"
#include "Graphics/RayTracing/Effect/RTEffectSet.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/Water/RayTracing/WaterReflectionRayTracingManager.h"
#include "Math/MathCore.h"
#include "Graphics/Render/RenderGraph.h"

namespace CoreEngine
{
    RT_EFFECT_REGISTER(WaterReflectionRayTracingManager,
        .id = "RTWaterReflection",
        .group = kWaterRTEffectGroup,
        .passes = {
            RTEffectPassDesc::Of<RTWaterReflectionPass>(RenderPassPhase::Water, 15),
        })

    RT_DEBUG_EFFECT_REGISTER(kReflectionEffect,
        .id = "RTWaterReflection",
        .label = "水の反射",
        .cvarPrefix = nullptr,
        .passNames = { "RTWaterReflectionPass" },
        .stageLabels = { "トレース" },
        .collect = [](const RTDebugQuery& query, std::vector<RTDebugEffectRow>& rows) {
            if (auto* manager = query.domain ? query.domain->GetRTEffects().Find<WaterReflectionRayTracingManager>() : nullptr) {
                rows.push_back({ "ゲームの画面", manager->GetDispatchInfo() });
            }
        });

    RT_DEBUG_CHANNEL_REGISTER(
        .effect = "水の反射",
        .stage = "トレース",
        .name = "映り込みの色",
        .description = "水面で跳ね返ったレイが見た色（水面が合成する）",
        .components = { "R", "G", "B", "A" },
        .rangeMin = 0.0f,
        .rangeMax = 1.0f,
        .viewMode = RTDebugViewMode::Color,
        .resolve = [](const RenderContext& context, uint32_t, RTDebugSource& out) {
            return ResolveFrameBlackboardImage(context, FrameBlackboard::RTWaterReflectionColor, out);
        })

    RT_DEBUG_CHANNEL_REGISTER(
        .effect = "水の反射",
        .stage = "トレース",
        .name = "水面の日の当たり方",
        .description = "水面の点から太陽へ撃ったレイの結果。0 = 物の影の中・1 = 日向（水面ではない画素は 1）",
        .components = { "日の当たり方" },
        .rangeMin = 0.0f,
        .rangeMax = 1.0f,
        .viewMode = RTDebugViewMode::Mono,
        .resolve = [](const RenderContext& context, uint32_t, RTDebugSource& out) {
            return ResolveFrameBlackboardImage(context, FrameBlackboard::RTWaterSunVisibility, out);
        })

    namespace {
        /// @brief 空キューブマップ（空＋雲）を引く
        /// @details レイが空へ抜けたときの色を、実際にトレースした向きでこのパスの中で決めるために使う
        ///          （Water.PS 側で平面法線の空と混ぜると二重像になる）
        D3D12_GPU_DESCRIPTOR_HANDLE ResolveSkyEnvironment(const RenderContext& context)
        {
            if (auto* atmosphere = context.atmosphereManager) {
                if (atmosphere->IsSkySpecularEnabled() && atmosphere->IsSkyEnvironmentReady()) {
                    return atmosphere->GetSkySpecularSRVHandle();
                }
            }
            return {};
        }

        /// @brief 水面の影を調べるメインライト（0 番。Water.PS が掛けるのも 0 番の項だけ）
        WaterSunShadowInput BuildSunShadowInput(const RenderContext& context)
        {
            WaterSunShadowInput sunShadow{};
            if (context.lightManager) {
                if (const Light* mainLight = context.lightManager->GetDirectionalLight(0);
                    mainLight && mainLight->enabled) {
                    sunShadow.direction = CoreEngine::Normalize(mainLight->direction);
                    sunShadow.enabled = true;
                }
            }
            return sunShadow;
        }
    }

    void RTWaterReflectionPass::DeclareResources(RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        builder.Read(FrameBlackboard::SceneDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Read(FrameBlackboard::SceneColorSnapshot, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        // 当たった点を照らすときの雲の影（雲を使わないシーンでは登録されない）
        builder.Read(FrameBlackboard::CloudShadowMap, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Write(FrameBlackboard::RTWaterReflectionColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        builder.Write(FrameBlackboard::RTWaterSunVisibility, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    void RTWaterReflectionPass::Execute(const RenderContext& context)
    {
        // 止めたフレームは撃たない（出力を載せないので、水面は映り込みなしで描く）
        if (!kReflectionEffect.IsActive()) {
            return;
        }
        WaterReflectionRayTracingManager* reflection = FindRTEffect<WaterReflectionRayTracingManager>(context);
        if (!context.dxCommon || !reflection || !reflection->IsInitialized() || !context.cmdList) {
            return;
        }

        // 水面が存在しない・非表示のフレームはディスパッチしない。
        // 出力を Blackboard へ登録しないため、Water.PS の反射合成も自動的に無効化される。
        if (!context.waterSurfaceState
            || context.waterSurfaceState->regionValid == 0) {
            return;
        }

        constexpr auto kView = WaterReflectionRayTracingManager::ViewID::GameView;
        const WaterSurfaceData& surfaceData = *context.waterSurfaceState;

        WaterRayTracingPassBase::FrameInput frame;
        if (WaterRayTracingPassBase::BuildFrameInput(context, surfaceData, "water reflection", true, frame)) {
            reflection->Dispatch(
                context.cmdList,
                frame.sceneDepthSRV,
                frame.sceneColorSRV,
                frame.viewProjection,
                frame.cameraPosition,
                surfaceData,
                frame.fftOceanInput,
                ResolveSkyEnvironment(context),
                BuildSunShadowInput(context),
                WaterRayTracingPassBase::BuildHitShadingInput(context),
                frame.width,
                frame.height,
                kView);
        }

        if (context.frameBlackboard) {
            context.frameBlackboard->SetResource(
                FrameBlackboard::RTWaterReflectionColor,
                reflection->GetReflectionSRVHandle(kView),
                &reflection->GetReflectionResource(kView));
            context.frameBlackboard->SetResource(
                FrameBlackboard::RTWaterSunVisibility,
                reflection->GetSunVisibilitySRVHandle(kView),
                &reflection->GetSunVisibilityResource(kView));
        }
    }
}
