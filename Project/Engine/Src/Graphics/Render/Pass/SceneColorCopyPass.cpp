#include "pch.h"
#include "SceneColorCopyPass.h"

#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"
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
            OutputDebugStringA("ERROR: SceneColorCopyPass: GraphicsCore is null in RenderContext!\n");
#endif
            assert(false && "SceneColorCopyPass requires GraphicsCore in RenderContext");
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

        GpuResource& source = sourceTarget->Resource();
        GpuResource& destination = destinationTarget->Resource();

        // コピー前後の 2 本ずつを 1 回の ResourceBarrier にまとめる
        {
            BarrierBatch batch(cmdList);
            batch.Transition(source, D3D12_RESOURCE_STATE_COPY_SOURCE);
            batch.Transition(destination, D3D12_RESOURCE_STATE_COPY_DEST);
        }

        cmdList->CopyResource(destination.Get(), source.Get());

        {
            BarrierBatch batch(cmdList);
            batch.Transition(destination, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            batch.Transition(source, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }

        // SceneColorSnapshot は RegisterFrameResources で同一の実体が登録済みのため、
        // 実行中の Blackboard 再登録は行わない（パス分離契約 3）。
    }
}
