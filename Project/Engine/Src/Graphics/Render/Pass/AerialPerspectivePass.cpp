#include "pch.h"
#include "AerialPerspectivePass.h"

#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Render/RenderTarget/OffscreenRenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Graphics/Render/RenderTarget/RenderTargetNames.h"
#include "Graphics/Render/RenderGraph.h"

namespace CoreEngine
{
    void AerialPerspectivePass::DeclareResources(RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        // SceneColor / SceneDepth を読み、SceneColor へ霞を合成する。
        // （SceneColor の SRV/COPY 遷移はパス内部で状態参照を通じて管理する）
        builder.Read(FrameBlackboard::SceneDepth, D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Read(FrameBlackboard::SceneColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Write(FrameBlackboard::SceneColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    void AerialPerspectivePass::Execute(const RenderContext& context)
    {
        // GameView のみで有効（水面反射などの補助 View には適用しない）
        if (context.viewSettings.viewType != RenderViewType::GameView) {
            return;
        }

        if (!context.dxCommon || !context.atmosphereManager || !context.renderTargetManager) {
            return;
        }

        ID3D12GraphicsCommandList* cmdList = context.dxCommon->GetCommandList();
        if (!cmdList) {
            return;
        }

        auto* sceneColorTarget = dynamic_cast<OffscreenRenderTarget*>(
            context.renderTargetManager->GetRenderTarget(context.viewSettings.sceneColorTargetName));
        if (!sceneColorTarget || !sceneColorTarget->GetResource()) {
            return;
        }

        context.atmosphereManager->ApplyAerialPerspective(
            cmdList,
            sceneColorTarget->GetResource(),
            sceneColorTarget->GetCurrentState(),
            sceneColorTarget->GetSRVHandle(),
            context.dxCommon->GetDepthStencilSRV());
    }
}
