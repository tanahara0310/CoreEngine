#include "pch.h"
#include "RTWaterSeabedPass.h"

#include "Graphics/Render/RenderGraph.h"
#include "Graphics/RayTracing/Debug/RTDebugRegistry.h"
#include "Graphics/RayTracing/Effect/RTEffectRegistry.h"
#include "Graphics/RayTracing/Effect/RTEffectSet.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/Water/RayTracing/WaterSeabedRayTracingManager.h"

namespace CoreEngine
{
    RT_EFFECT_REGISTER(WaterSeabedRayTracingManager,
        .id = "RTWaterSeabed",
        .group = kWaterRTEffectGroup,
        .passes = {
            RTEffectPassDesc::Of<RTWaterSeabedPass>(RenderPassPhase::Water, 5),
        })

    RT_DEBUG_EFFECT_REGISTER(kSeabedEffect,
        .id = "RTWaterSeabed",
        .label = "海底の高さ",
        .cvarPrefix = nullptr,
        .passNames = { "RTWaterSeabedPass" },
        .stageLabels = { "トレース" },
        .collect = [](const RTDebugQuery& query, std::vector<RTDebugEffectRow>& rows) {
            if (auto* manager = query.domain ? query.domain->GetRTEffects().Find<WaterSeabedRayTracingManager>() : nullptr) {
                rows.push_back({ "ゲームの画面", manager->GetDispatchInfo() });
            }
        });

    RT_DEBUG_CHANNEL_REGISTER(
        .effect = "海底の高さ",
        .stage = "トレース",
        .name = "高さ",
        .description = "カメラの周りの範囲を真上から撃って測った海底の高さ（m）。岸の泡が読む。画面の並びではない",
        .components = { "高さ" },
        .rangeMin = -10.0f,
        .rangeMax = 2.0f,
        .viewMode = RTDebugViewMode::FalseColor,
        .screenSpace = false,
        .resolve = [](const RenderContext& context, uint32_t, RTDebugSource& out) {
            return ResolveFrameBlackboardImage(context, FrameBlackboard::RTWaterSeabedHeight, out);
        })

    void RTWaterSeabedPass::DeclareResources(RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        builder.Write(FrameBlackboard::RTWaterSeabedHeight, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    void RTWaterSeabedPass::Execute(const RenderContext& context)
    {
        // 止めたフレームは測らない（出力を載せないので、岸の泡は海底を読まない）
        if (!kSeabedEffect.IsActive()) {
            return;
        }
        // 水面が無いフレームは測らない（水面も描かれない）
        if (!context.waterSurfaceState || context.waterSurfaceState->regionValid == 0) {
            return;
        }
        WaterSeabedRayTracingManager* seabed = FindRTEffect<WaterSeabedRayTracingManager>(context);
        if (!context.dxCommon || !context.cmdList || !seabed || !seabed->IsInitialized()) {
            return;
        }

        constexpr auto kView = WaterSeabedRayTracingManager::ViewID::GameView;
        const WaterSurfaceData& surfaceData = *context.waterSurfaceState;

        WaterRayTracingPassBase::FrameInput frame;
        if (WaterRayTracingPassBase::BuildFrameInput(context, surfaceData, "water seabed", false, frame)) {
            // Gerstner 経路では変位テクスチャを読まないが、宣言されたスロットには何かを差す
            if (frame.fftOceanInput.displacementSRV.ptr == 0) {
                frame.fftOceanInput.displacementSRV = frame.sceneDepthSRV;
            }
            seabed->Dispatch(context.cmdList, surfaceData, frame.fftOceanInput, kView);
        }

        if (context.frameBlackboard && seabed->GetSeabedSRVHandle(kView).ptr != 0) {
            context.frameBlackboard->SetResource(
                FrameBlackboard::RTWaterSeabedHeight,
                seabed->GetSeabedSRVHandle(kView),
                &seabed->GetSeabedResource(kView));
        }
    }
}
