#include "pch.h"
#include "RTWaterRefractionPass.h"

#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RayTracing/Debug/RTDebugRegistry.h"
#include "Graphics/RayTracing/Effect/RTEffectRegistry.h"
#include "Graphics/RayTracing/Effect/RTEffectSet.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/Render/RenderingTechnique/Lighting/DeferredLightingTechnique.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueManager.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueNames.h"
#include "Graphics/Water/RayTracing/WaterCausticsRayTracingManager.h"
#include "Graphics/Water/RayTracing/WaterRefractionRayTracingManager.h"
#include "Utility/Logger/Logger.h"
#include "Graphics/Render/RenderGraph.h"

namespace CoreEngine
{
    RT_EFFECT_REGISTER(WaterRefractionRayTracingManager,
        .id = "RTWaterRefraction",
        .group = kWaterRTEffectGroup,
        .passes = {
            RTEffectPassDesc::Of<RTWaterRefractionPass>(RenderPassPhase::Water, 10),
        })

    RT_DEBUG_EFFECT_REGISTER(kRefractionEffect,
        .id = "RTWaterRefraction",
        .label = "水の屈折",
        .cvarPrefix = "r.Water.Refraction",
        .passNames = { "RTWaterRefractionPass" },
        .stageLabels = { "トレース" },
        .collect = [](const RTDebugQuery& query, std::vector<RTDebugEffectRow>& rows) {
            if (auto* manager = query.domain ? query.domain->GetRTEffects().Find<WaterRefractionRayTracingManager>() : nullptr) {
                rows.push_back({ "ゲームの画面", manager->GetDispatchInfo() });
            }
        });

    RT_DEBUG_CHANNEL_REGISTER(
        .effect = "水の屈折",
        .stage = "トレース",
        .name = "屈折の色",
        .description = "水面から水の中へ曲がったレイが見た色（水面が合成する）",
        .components = { "R", "G", "B", "A" },
        .rangeMin = 0.0f,
        .rangeMax = 1.0f,
        .viewMode = RTDebugViewMode::Color,
        .resolve = [](const RenderContext& context, uint32_t, RTDebugSource& out) {
            return ResolveFrameBlackboardImage(context, FrameBlackboard::RTWaterRefractionColor, out);
        })

    namespace {
        /// @brief 水中の点の照らし方を、このフレームの DeferredLighting が水中の点に使った値にそろえる
        WaterUnderwaterLightingInput BuildUnderwaterLightingInput(const RenderContext& context)
        {
            WaterUnderwaterLightingInput underwaterLighting{};
            const WaterCausticsRayTracingManager* caustics = FindRTEffect<WaterCausticsRayTracingManager>(context);
            if (!context.renderingTechniqueManager || !caustics) {
                return underwaterLighting;
            }
            auto* deferredLighting = context.renderingTechniqueManager->GetTechnique<DeferredLightingTechnique>(
                RenderingTechniqueNames::DeferredLighting);
            if (!deferredLighting) {
                return underwaterLighting;
            }
            const DeferredLightingTechnique::WaterCausticsDebugSettings& waterLighting =
                deferredLighting->GetWaterCausticsDebugSettings();
            underwaterLighting.enabled = (waterLighting.waterVolumeEnabled != 0);
            for (int c = 0; c < 3; ++c) {
                underwaterLighting.absorptionCoeff[c] = waterLighting.absorptionCoeff[c];
            }
            underwaterLighting.causticsIntensityScale = caustics->GetSettings().intensityScale;
            return underwaterLighting;
        }
    }

    void RTWaterRefractionPass::DeclareResources(RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        builder.Read(FrameBlackboard::SceneDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Read(FrameBlackboard::SceneColorSnapshot, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        // 当たった点を照らすときの雲の影（雲を使わないシーンでは登録されない）
        builder.Read(FrameBlackboard::CloudShadowMap, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Write(FrameBlackboard::RTWaterRefractionColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    void RTWaterRefractionPass::Execute(const RenderContext& context)
    {
        // 止めたフレームは撃たない（出力を載せないので、水面は屈折なしで描く）
        if (!kRefractionEffect.IsActive()) {
            return;
        }
        // 効果は水面のあるシーンで作られる（水面の無いシーンでは何もしない）
        WaterRefractionRayTracingManager* refraction = FindRTEffect<WaterRefractionRayTracingManager>(context);
        if (!refraction) {
            return;
        }
        if (!context.dxCommon) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "RTWaterRefractionPass: skipped. dxCommon is null.");
            return;
        }

        if (!refraction->IsInitialized()) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "RTWaterRefractionPass: skipped. manager is not initialized.");
            return;
        }

        // 水面が存在しない・非表示のフレームはディスパッチしない（regionValid=0 = 有効な水域なし）
        if (!context.waterSurfaceState
            || context.waterSurfaceState->regionValid == 0) {
            return;
        }

        if (!context.cmdList) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Command,
                "RTWaterRefractionPass: skipped. command list is null.");
            return;
        }

        // IsEnabledForView が GameView 限定のため、ここに来るのは常に GameView
        const WaterRefractionRayTracingManager::ViewID viewId =
            WaterRefractionRayTracingManager::ViewID::GameView;

        const WaterSurfaceData& surfaceData = *context.waterSurfaceState;

        WaterRayTracingPassBase::FrameInput frame;
        if (WaterRayTracingPassBase::BuildFrameInput(context, surfaceData, "water refraction", true, frame)) {
            refraction->Dispatch(
                context.cmdList,
                frame.sceneDepthSRV,
                frame.sceneColorSRV,
                frame.viewProjection,
                frame.cameraPosition,
                surfaceData,
                frame.fftOceanInput,
                WaterRayTracingPassBase::BuildHitShadingInput(context),
                BuildUnderwaterLightingInput(context),
                frame.width,
                frame.height,
                viewId);
        }

        if (context.frameBlackboard) {
            D3D12_GPU_DESCRIPTOR_HANDLE handle = refraction->GetRefractionSRVHandle(viewId);
            context.frameBlackboard->SetResource(
                FrameBlackboard::RTWaterRefractionColor,
                handle,
                &refraction->GetRefractionResource(viewId));
        }

        // 診断ログは UI の「RT屈折ログを有効にする」でのみ出す
        if (refraction->GetSettings().debugLogEnabled != 0) {
            const auto& diagnostics = refraction->GetDispatchInfo();
            Logger::GetInstance().Infof(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "RTWaterRefractionPass: status={} viewIndex={} waterHeight={:.3f} size={}x{} blasCount={} outputSRV=0x{:X}",
                ToString(diagnostics.status),
                diagnostics.viewIndex,
                refraction->GetLastWaterHeight(),
                diagnostics.width,
                diagnostics.height,
                diagnostics.blasCount,
                diagnostics.outputSrvPtr);
        }
    }
}
