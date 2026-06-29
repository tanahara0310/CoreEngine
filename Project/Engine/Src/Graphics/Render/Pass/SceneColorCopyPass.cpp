#include "pch.h"
#include "SceneColorCopyPass.h"

#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Common/ResourceBarrierHelper.h"
#include "Graphics/Render/RenderTarget/OffscreenRenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include <cassert>

namespace CoreEngine
{
    void SceneColorCopyPass::Execute(const RenderContext& context)
    {
        if (!context.renderTargetManager) {
#ifdef _DEBUG
            OutputDebugStringA("ERROR: SceneColorCopyPass: RenderTargetManager is null in RenderContext!\n");
#endif
            assert(false && "SceneColorCopyPass requires RenderTargetManager in RenderContext");
            return;
        }

        if (!context.dxCommon) {
#ifdef _DEBUG
            OutputDebugStringA("ERROR: SceneColorCopyPass: DirectXCommon is null in RenderContext!\n");
#endif
            assert(false && "SceneColorCopyPass requires DirectXCommon in RenderContext");
            return;
        }

        auto* sourceTarget = dynamic_cast<OffscreenRenderTarget*>(
            context.renderTargetManager->GetRenderTarget(sourceTargetName_));
        auto* destinationTarget = dynamic_cast<OffscreenRenderTarget*>(
            context.renderTargetManager->GetRenderTarget(destinationTargetName_));

        if (!sourceTarget || !destinationTarget ||
            !sourceTarget->GetResource() || !destinationTarget->GetResource()) {
            return;
        }

        auto* cmdList = context.dxCommon->GetCommandList();
        if (!cmdList) {
            return;
        }

        D3D12_RESOURCE_STATES& sourceState = sourceTarget->GetCurrentState();
        D3D12_RESOURCE_STATES& destinationState = destinationTarget->GetCurrentState();

        ResourceBarrierHelper::Transition(
            cmdList,
            sourceTarget->GetResource(),
            sourceState,
            D3D12_RESOURCE_STATE_COPY_SOURCE);
        ResourceBarrierHelper::Transition(
            cmdList,
            destinationTarget->GetResource(),
            destinationState,
            D3D12_RESOURCE_STATE_COPY_DEST);

        cmdList->CopyResource(destinationTarget->GetResource(), sourceTarget->GetResource());

        ResourceBarrierHelper::Transition(
            cmdList,
            destinationTarget->GetResource(),
            destinationState,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        ResourceBarrierHelper::Transition(
            cmdList,
            sourceTarget->GetResource(),
            sourceState,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        if (context.frameBlackboard) {
            context.frameBlackboard->SetResource(
                FrameBlackboard::SceneColorSnapshot,
                destinationTarget->GetSRVHandle(),
                destinationTarget->GetResource(),
                &destinationState);
        }
    }
}
