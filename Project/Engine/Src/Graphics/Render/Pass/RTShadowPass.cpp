#include "pch.h"
#include "RTShadowPass.h"

#include "EngineSystem/Subsystem/RayTracingSubsystem.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/RayTracing/RayTracingShadowManager.h"

namespace CoreEngine
{
    void RTShadowPass::Execute(const RenderContext& context)
    {
        if (!context.rayTracingSubsystem || !context.dxCommon || !context.rtShadowManager) {
            output_.Reset();
            return;
        }

        if (!context.rtShadowManager->IsInitialized()) {
            output_.Reset();
            return;
        }

        ID3D12GraphicsCommandList* cmdList = context.dxCommon->GetCommandList();
        if (!cmdList) {
            output_.Reset();
            return;
        }

        // 現在の描画対象ビューに合わせて RT シャドウ出力先を切り替える。
        const RayTracingShadowManager::ViewID viewId =
            (context.currentRTShadowViewId == static_cast<uint32_t>(RayTracingShadowManager::ViewID::SceneView))
            ? RayTracingShadowManager::ViewID::SceneView
            : RayTracingShadowManager::ViewID::GameView;

        // GBuffer を入力に RT シャドウをディスパッチする。
        context.rayTracingSubsystem->DispatchRTShadow(
            context,
            context.dxCommon,
            cmdList,
            viewId);

        if (context.frameBlackboard) {
            // Blackboard に RT シャドウ出力の実リソースと現在状態参照を公開する。
            D3D12_GPU_DESCRIPTOR_HANDLE shadowHandle = context.rtShadowManager->GetShadowSRVHandle(viewId, 0);
            ID3D12Resource* shadowResource = context.rtShadowManager->GetShadowResource(viewId, 0);
            D3D12_RESOURCE_STATES& currentState = context.rtShadowManager->GetShadowCurrentState(viewId, 0);
            context.frameBlackboard->SetResource(
                FrameBlackboard::RTShadowMask,
                shadowHandle,
                shadowResource,
                &currentState);
        }

        output_.Reset();
    }
}
