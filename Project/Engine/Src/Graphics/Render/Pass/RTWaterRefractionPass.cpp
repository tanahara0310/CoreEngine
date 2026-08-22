#include "pch.h"
#include "RTWaterRefractionPass.h"

#include "EngineSystem/Subsystem/RayTracingSubsystem.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/Water/RayTracing/WaterRefractionRayTracingManager.h"
#include "Utility/Logger/Logger.h"
#include "Graphics/Render/RenderGraph.h"

namespace CoreEngine
{
    void RTWaterRefractionPass::DeclareResources(RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        builder.Read(FrameBlackboard::SceneDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Read(FrameBlackboard::SceneColorSnapshot, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Write(FrameBlackboard::RTWaterRefractionColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

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

        // 水面が存在しない・非表示のフレームはディスパッチしない（regionValid=0 = 有効な水域なし）。
        // RTWaterCausticsPass は同じガードを持っていたが、屈折・反射は持っておらず
        // 水面が消えていてもフル解像度で DispatchRays が走っていた。
        if (!context.waterSurfaceState
            || context.waterSurfaceState->regionValid == 0) {
            return;
        }

        ID3D12GraphicsCommandList* cmdList = context.cmdList;
        if (!cmdList) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Command,
                "RTWaterRefractionPass: skipped. command list is null.");
            return;
        }

        // IsEnabledForView が GameView 限定のため、ここに来るのは常に GameView。
        // 反射ビュー対応を追加する場合は IsEnabledForView 側と同時に見直すこと。
        const WaterRefractionRayTracingManager::ViewID viewId =
            WaterRefractionRayTracingManager::ViewID::GameView;

        const WaterSurfaceData& dispatchSurfaceData = *context.waterSurfaceState;

        context.rayTracingSubsystem->DispatchWaterRefraction(
            context,
            context.dxCommon,
            cmdList,
            viewId,
            dispatchSurfaceData);

        if (context.frameBlackboard) {
            D3D12_GPU_DESCRIPTOR_HANDLE handle = context.rtWaterRefractionManager->GetRefractionSRVHandle(viewId);
            ID3D12Resource* resource = context.rtWaterRefractionManager->GetRefractionResource(viewId);
            D3D12_RESOURCE_STATES& currentState = context.rtWaterRefractionManager->GetRefractionCurrentState(viewId);
            context.frameBlackboard->SetResource(
                FrameBlackboard::RTWaterRefractionColor,
                handle,
                resource,
                &currentState);
        }

        // 診断ログは UI の「RT屈折ログを有効にする」でのみ出す
        // （以前は毎フレーム 3 本が無条件に流れていた）
        if (context.rtWaterRefractionManager->GetSettings().debugLogEnabled != 0) {
            const auto& diagnostics = context.rtWaterRefractionManager->GetDispatchInfo();
            Logger::GetInstance().Infof(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "RTWaterRefractionPass: status={} viewIndex={} waterHeight={:.3f} size={}x{} blasCount={} outputSRV=0x{:X}",
                ToString(diagnostics.status),
                diagnostics.viewIndex,
                context.rtWaterRefractionManager->GetLastWaterHeight(),
                diagnostics.width,
                diagnostics.height,
                diagnostics.blasCount,
                diagnostics.outputSrvPtr);
        }
    }
}
