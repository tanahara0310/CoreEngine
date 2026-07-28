#include "pch.h"
#include "RTWaterReflectionPass.h"

#include "EngineSystem/Subsystem/RayTracingSubsystem.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Water/RayTracing/WaterReflectionRayTracingManager.h"
#include "Utility/Logger/Logger.h"
#include "Graphics/Render/RenderGraph.h"

namespace CoreEngine
{
    void RTWaterReflectionPass::DeclareResources(RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        builder.Read(FrameBlackboard::SceneDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Read(FrameBlackboard::SceneColorSnapshot, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Write(FrameBlackboard::RTWaterReflectionColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    void RTWaterReflectionPass::Execute(const RenderContext& context)
    {
        if (!context.rayTracingSubsystem || !context.dxCommon || !context.rtWaterReflectionManager) {
            return;
        }

        if (!context.rtWaterReflectionManager->IsInitialized()) {
            return;
        }

        ID3D12GraphicsCommandList* cmdList = context.dxCommon->GetCommandList();
        if (!cmdList) {
            return;
        }

        // 水面が存在しない・非表示のフレームはディスパッチしない（RTWaterCausticsPass と同じガード）。
        // 出力を Blackboard へ登録しないため、Water.PS の反射合成も自動的に無効化される。
        if (!context.waterSurfaceState
            || context.waterSurfaceState->regionValid == 0) {
            return;
        }

        const WaterSurfaceData& dispatchSurfaceData = *context.waterSurfaceState;

        context.rayTracingSubsystem->DispatchWaterReflection(
            context,
            context.dxCommon,
            cmdList,
            WaterReflectionRayTracingManager::ViewID::GameView,
            dispatchSurfaceData);

        if (context.frameBlackboard) {
            D3D12_GPU_DESCRIPTOR_HANDLE handle =
                context.rtWaterReflectionManager->GetReflectionSRVHandle(
                    WaterReflectionRayTracingManager::ViewID::GameView);
            ID3D12Resource* resource =
                context.rtWaterReflectionManager->GetReflectionResource(
                    WaterReflectionRayTracingManager::ViewID::GameView);
            D3D12_RESOURCE_STATES& currentState =
                context.rtWaterReflectionManager->GetReflectionCurrentState(
                    WaterReflectionRayTracingManager::ViewID::GameView);
            context.frameBlackboard->SetResource(
                FrameBlackboard::RTWaterReflectionColor,
                handle,
                resource,
                &currentState);
        }
    }
}
