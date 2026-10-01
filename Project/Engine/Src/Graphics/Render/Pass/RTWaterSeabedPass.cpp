#include "pch.h"
#include "RTWaterSeabedPass.h"

#include "EngineSystem/Subsystem/RayTracingSubsystem.h"
#include "Graphics/Render/RenderGraph.h"
#include "Graphics/Water/RayTracing/WaterSeabedRayTracingManager.h"

namespace CoreEngine
{
    void RTWaterSeabedPass::DeclareResources(RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        builder.Write(FrameBlackboard::RTWaterSeabedHeight, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    void RTWaterSeabedPass::Execute(const RenderContext& context)
    {
        // 水面が無いフレームは測らない（水面も描かれない）
        if (!context.waterSurfaceState || context.waterSurfaceState->regionValid == 0) {
            return;
        }
        auto* seabed = context.rtWaterSeabedManager;
        if (!context.rayTracingSubsystem || !context.dxCommon || !context.cmdList
            || !seabed || !seabed->IsInitialized()) {
            return;
        }

        constexpr auto kView = WaterSeabedRayTracingManager::ViewID::GameView;
        context.rayTracingSubsystem->DispatchWaterSeabed(
            context,
            context.dxCommon,
            context.cmdList,
            kView,
            *context.waterSurfaceState);

        if (context.frameBlackboard && seabed->GetSeabedSRVHandle(kView).ptr != 0) {
            context.frameBlackboard->SetResource(
                FrameBlackboard::RTWaterSeabedHeight,
                seabed->GetSeabedSRVHandle(kView),
                &seabed->GetSeabedResource(kView));
        }
    }
}
