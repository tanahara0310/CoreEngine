#include "pch.h"
#include "RTWaterRefractionPass.h"

#include "EngineSystem/Subsystem/RayTracingSubsystem.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/RayTracing/WaterRefractionRayTracingManager.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    void RTWaterRefractionPass::Execute(const RenderContext& context)
    {
        if (!context.rayTracingSubsystem || !context.dxCommon || !context.rtWaterRefractionManager) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "RTWaterRefractionPass: skipped. rayTracingSubsystem={} dxCommon={} manager={}",
                context.rayTracingSubsystem != nullptr,
                context.dxCommon != nullptr,
                context.rtWaterRefractionManager != nullptr);
            return;
        }

        if (!context.rtWaterRefractionManager->IsInitialized()) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "RTWaterRefractionPass: skipped. manager is not initialized.");
            return;
        }

        ID3D12GraphicsCommandList* cmdList = context.dxCommon->GetCommandList();
        if (!cmdList) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Command,
                "RTWaterRefractionPass: skipped. command list is null.");
            return;
        }

        const WaterRefractionRayTracingManager::ViewID viewId =
            (context.currentRTShadowViewId == static_cast<uint32_t>(WaterRefractionRayTracingManager::ViewID::ReflectionView))
            ? WaterRefractionRayTracingManager::ViewID::ReflectionView
            : WaterRefractionRayTracingManager::ViewID::GameView;

        WaterSurfaceData defaultSurfaceData{};
        const WaterSurfaceData& dispatchSurfaceData = context.waterRefractionSurfaceData
            ? *context.waterRefractionSurfaceData
            : defaultSurfaceData;
        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::Pipeline,
            "RTWaterRefractionPass: execute begin. viewId={} currentRTShadowViewId={} dispatchWaterHeight={:.3f} activeWaveCount={} waveTime={:.3f}",
            static_cast<uint32_t>(viewId),
            context.currentRTShadowViewId,
            dispatchSurfaceData.waterHeight,
            dispatchSurfaceData.activeWaveCount,
            dispatchSurfaceData.time);

        context.rayTracingSubsystem->DispatchWaterRefraction(
            context,
            context.dxCommon,
            cmdList,
            viewId,
            dispatchSurfaceData);

        const WaterRefractionRayTracingManager::DispatchDiagnostics& diagnostics =
            context.rtWaterRefractionManager->GetLastDiagnostics();
        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::Pipeline,
            "RTWaterRefractionPass: execute end. diagStatus={} diagViewId={} waterHeight={:.3f} size={}x{} blasCount={} outputSRV=0x{:X}",
            static_cast<uint32_t>(diagnostics.status),
            static_cast<uint32_t>(diagnostics.viewId),
            diagnostics.waterHeight,
            diagnostics.width,
            diagnostics.height,
            diagnostics.blasCount,
            diagnostics.outputSrv);

        if (context.frameBlackboard) {
            D3D12_GPU_DESCRIPTOR_HANDLE handle = context.rtWaterRefractionManager->GetRefractionSRVHandle(viewId);
            ID3D12Resource* resource = context.rtWaterRefractionManager->GetRefractionResource(viewId);
            D3D12_RESOURCE_STATES& currentState = context.rtWaterRefractionManager->GetRefractionCurrentState(viewId);
            context.frameBlackboard->SetResource(
                FrameBlackboard::RTWaterRefractionColor,
                handle,
                resource,
                &currentState);

            Logger::GetInstance().Infof(
                LogCategory::Graphics,
                LogSubCategory::RenderTarget,
                "RTWaterRefractionPass: blackboard updated. handle=0x{:X} resource={} state={}",
                handle.ptr,
                resource != nullptr,
                static_cast<uint32_t>(currentState));
        }
    }
}
