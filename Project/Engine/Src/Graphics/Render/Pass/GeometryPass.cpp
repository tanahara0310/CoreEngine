#include "pch.h"
#include "GeometryPass.h"
#include "Graphics/Render/Render.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Common/Core/DepthStencilManager.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Scene/SceneManager.h"
#include "Graphics/Render/RenderGraph.h"
#include <cassert>

namespace CoreEngine
{
    void ForwardQueuePassBase::DeclareResources(RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        // SceneColor に上乗せしつつ SceneDepth を read-only DSV/SRV として参照する。
        builder.Read(FrameBlackboard::SceneColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        builder.Read(FrameBlackboard::SceneDepth, D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        builder.Write(FrameBlackboard::SceneColor, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }

    void ForwardQueuePassBase::ConfigureForView(const RenderContext& context)
    {
        SetRenderTargetName(context.viewSettings.sceneColorTargetName);
    }

    void ForwardQueuePassBase::Execute(const RenderContext& context)
    {
        if (!context.renderTargetManager) {
#ifdef _DEBUG
            OutputDebugStringA("ERROR: ForwardQueuePass: RenderTargetManager is null in RenderContext!\n");
#endif
            assert(false && "ForwardQueuePass requires RenderTargetManager in RenderContext");
            return;
        }

        RenderTarget* targetToUse = context.renderTargetManager->GetRenderTarget(targetName_);
        if (!targetToUse) {
#ifdef _DEBUG
            std::string msg = "ERROR: ForwardQueuePass: RenderTarget '" + targetName_ + "' not found!\n";
            OutputDebugStringA(msg.c_str());
#endif
            assert(false && "ForwardQueuePass requires a valid RenderTarget.");
            return;
        }

        if (!context.dxCommon) {
            assert(false && "ForwardQueuePass requires DirectXCommon in RenderContext");
            return;
        }

        auto* cmdList = context.cmdList;

        // DeferredLightingPass が先に書き込んでいる場合はクリアしない
        targetToUse->SetClearEnabled(ShouldClear());

        targetToUse->Begin(cmdList);

        if (context.renderManager) {
            // パス分離契約 2: 描画に必要な状態は先行パスに依存せず自分で設定する。
            // 描画に使うビューは EngineSystem がフレーム先頭で RenderManager へ渡し済み
            // （FrameViews）。パスごとにカメラを差し込む必要はない。
            context.renderManager->SetDebugLineRenderingEnabled(true);

            DrawQueue(context);
        }

        targetToUse->End(cmdList);

        // SceneColor は RegisterFrameResources で同一の実体が登録済みのため、
        // 実行中の Blackboard 再登録は行わない（パス分離契約 3）。
    }

    void GeometryPass::DrawQueue(const RenderContext& context)
    {
        context.renderManager->DrawMainQueuePass(context.cmdList, context.viewSettings.viewType);
    }

    void SkyBoxQueuePass::DrawQueue(const RenderContext& context)
    {
        context.renderManager->DrawSkyQueuePass(context.cmdList, context.viewSettings.viewType);
    }

    void TransparentQueuePass::DrawQueue(const RenderContext& context)
    {
        context.renderManager->DrawTransparentQueuePass(context.cmdList, context.viewSettings.viewType);
    }
}
