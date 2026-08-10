#include "pch.h"
#include "SceneColorCopyPass.h"

#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Common/ResourceBarrierHelper.h"
#include "Graphics/Render/RenderTarget/OffscreenRenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Graphics/Render/RenderGraph.h"
#include <cassert>

namespace CoreEngine
{
    void SceneColorCopyPass::DeclareResources(RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        // 水面が参照する背景として、forward 側の SkyBox/透明物を含む完成済み SceneColor を複製する。
        builder.Read(FrameBlackboard::SceneColor, D3D12_RESOURCE_STATE_COPY_SOURCE);
        builder.Write(FrameBlackboard::SceneColorSnapshot, D3D12_RESOURCE_STATE_COPY_DEST);
    }

    void SceneColorCopyPass::ConfigureForView(const RenderContext& context)
    {
        SetSourceTargetName(context.viewSettings.sceneColorTargetName);
        SetDestinationTargetName(RenderTargetNames::SceneColorSnapshot);
    }

    bool SceneColorCopyPass::IsEnabledForView(const RenderViewSettings& view) const
    {
        return view.viewType == RenderViewType::GameView
            && view.sceneColorTargetName == RenderTargetNames::SceneColor;
    }

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

        auto* cmdList = context.cmdList;
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

        // SceneColorSnapshot は RegisterFrameResources で同一の実体が登録済みのため、
        // 実行中の Blackboard 再登録は行わない（パス分離契約 3）。
    }
}
