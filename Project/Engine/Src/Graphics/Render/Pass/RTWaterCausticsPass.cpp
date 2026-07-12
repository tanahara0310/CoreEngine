#include "pch.h"
#include "RTWaterCausticsPass.h"

#include "EngineSystem/Subsystem/RayTracingSubsystem.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Water/RayTracing/WaterCausticsRayTracingManager.h"
#include "Utility/Logger/Logger.h"
#include "Graphics/Render/RenderGraph.h"

namespace CoreEngine
{
    void RTWaterCausticsPass::DeclareResources(RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        builder.Read(FrameBlackboard::GBufferWorldPosition, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Write(FrameBlackboard::RTWaterCaustics, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    void RTWaterCausticsPass::Execute(const RenderContext& context)
    {
        if (!context.rayTracingSubsystem || !context.dxCommon || !context.rtWaterCausticsManager) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "RTWaterCausticsPass: skipped. rayTracingSubsystem={} dxCommon={} manager={}",
                context.rayTracingSubsystem != nullptr,
                context.dxCommon != nullptr,
                context.rtWaterCausticsManager != nullptr);
            return;
        }

        if (!context.rtWaterCausticsManager->IsInitialized()) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "RTWaterCausticsPass: skipped. manager is not initialized.");
            return;
        }

        ID3D12GraphicsCommandList* cmdList = context.dxCommon->GetCommandList();
        if (!cmdList) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Command,
                "RTWaterCausticsPass: skipped. command list is null.");
            return;
        }

        const WaterCausticsRayTracingManager::ViewID viewId =
            (context.currentRTShadowViewId == static_cast<uint32_t>(WaterCausticsRayTracingManager::ViewID::ReflectionView))
            ? WaterCausticsRayTracingManager::ViewID::ReflectionView
            : WaterCausticsRayTracingManager::ViewID::GameView;

        WaterSurfaceData defaultSurfaceData{};
        const WaterSurfaceData& dispatchSurfaceData = context.waterRefractionSurfaceData
            ? *context.waterRefractionSurfaceData
            : defaultSurfaceData;

        context.rayTracingSubsystem->DispatchWaterCaustics(
            context,
            context.dxCommon,
            cmdList,
            viewId,
            dispatchSurfaceData);

        const WaterCausticsRayTracingManager::DispatchDiagnostics& diagnostics =
            context.rtWaterCausticsManager->GetLastDiagnostics();
        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::Pipeline,
            "RTWaterCausticsPass: execute end. diagStatus={} diagViewId={} waterHeight={:.3f} activeWaveCount={} size={}x{} blasCount={} outputSRV=0x{:X}",
            static_cast<uint32_t>(diagnostics.status),
            static_cast<uint32_t>(diagnostics.viewId),
            diagnostics.waterHeight,
            diagnostics.activeWaveCount,
            diagnostics.width,
            diagnostics.height,
            diagnostics.blasCount,
            diagnostics.outputSrv);

        if (context.frameBlackboard) {
            D3D12_GPU_DESCRIPTOR_HANDLE handle = context.rtWaterCausticsManager->GetCausticsSRVHandle(viewId);
            ID3D12Resource* resource = context.rtWaterCausticsManager->GetCausticsResource(viewId);
            D3D12_RESOURCE_STATES& currentState = context.rtWaterCausticsManager->GetCausticsCurrentState(viewId);
            context.frameBlackboard->SetResource(
                FrameBlackboard::RTWaterCaustics,
                handle,
                resource,
                &currentState);
        }
    }
}
