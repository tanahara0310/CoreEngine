#include "pch.h"
#include "GeometryPass.h"
#include "Graphics/Render/Render.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Common/ResourceBarrierHelper.h"
#include "Graphics/Common/Core/DepthStencilManager.h"
#include "Graphics/Render/RenderTarget/OffscreenRenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Graphics/Render/RenderTarget/RenderTargetNames.h"
#include <cassert>

namespace CoreEngine
{
    void GeometryPass::Execute(const RenderContext& context)
    {
        // RenderTargetManagerが必要
        if (!context.renderTargetManager) {
#ifdef _DEBUG
            OutputDebugStringA("ERROR: GeometryPass: RenderTargetManager is null in RenderContext!\n");
#endif
            assert(false && "GeometryPass requires RenderTargetManager in RenderContext");
            return;
        }

        // 名前ベースでターゲットを取得
        RenderTarget* targetToUse = context.renderTargetManager->GetRenderTarget(targetName_);

        if (!targetToUse) {
#ifdef _DEBUG
            std::string msg = "ERROR: GeometryPass: RenderTarget '" + targetName_ + "' not found in RenderTargetManager!\n";
            OutputDebugStringA(msg.c_str());
#endif
            assert(false && "GeometryPass requires a valid RenderTarget.");
            return;
        }

        // DirectXCommonが必要
        if (!context.dxCommon) {
#ifdef _DEBUG
            OutputDebugStringA("ERROR: GeometryPass: DirectXCommon is null in RenderContext!\n");
#endif
            assert(false && "GeometryPass requires DirectXCommon in RenderContext");
            return;
        }

        auto* cmdList = context.dxCommon->GetCommandList();

        // DeferredLightingPass が先に書き込んでいる場合はクリアしない
        targetToUse->SetClearEnabled(clearEnabled_);

        targetToUse->Begin(cmdList);
        if (renderCallback_) {
            renderCallback_();
        }

        const bool shouldUpdateSceneColorSnapshot =
            context.viewSettings.viewType == RenderViewType::GameView
            && targetName_ == RenderTargetNames::SceneColor;

        if (shouldUpdateSceneColorSnapshot) {
            auto* sourceTarget = dynamic_cast<OffscreenRenderTarget*>(targetToUse);
            auto* snapshotTarget = dynamic_cast<OffscreenRenderTarget*>(
                context.renderTargetManager->GetRenderTarget(RenderTargetNames::SceneColorSnapshot));

            if (sourceTarget && snapshotTarget && sourceTarget->GetResource() && snapshotTarget->GetResource()) {
                D3D12_RESOURCE_STATES& sourceState = sourceTarget->GetCurrentState();
                D3D12_RESOURCE_STATES& snapshotState = snapshotTarget->GetCurrentState();

                ResourceBarrierHelper::Transition(
                    cmdList,
                    sourceTarget->GetResource(),
                    sourceState,
                    D3D12_RESOURCE_STATE_COPY_SOURCE);
                ResourceBarrierHelper::Transition(
                    cmdList,
                    snapshotTarget->GetResource(),
                    snapshotState,
                    D3D12_RESOURCE_STATE_COPY_DEST);

                cmdList->CopyResource(snapshotTarget->GetResource(), sourceTarget->GetResource());

                ResourceBarrierHelper::Transition(
                    cmdList,
                    snapshotTarget->GetResource(),
                    snapshotState,
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                ResourceBarrierHelper::Transition(
                    cmdList,
                    sourceTarget->GetResource(),
                    sourceState,
                    D3D12_RESOURCE_STATE_RENDER_TARGET);
            }
        }

        if (waterRenderCallback_) {
            waterRenderCallback_();
        }
        targetToUse->End(cmdList);

        if (context.frameBlackboard) {
            D3D12_RESOURCE_STATES* stateRef = nullptr;
            if (auto* offscreen = dynamic_cast<OffscreenRenderTarget*>(targetToUse)) {
                stateRef = &offscreen->GetCurrentState();
            }
            context.frameBlackboard->SetResource(
                FrameBlackboard::SceneColor,
                targetToUse->GetSRVHandle(),
                targetToUse->GetResource(),
                stateRef);
        }
    }
}
