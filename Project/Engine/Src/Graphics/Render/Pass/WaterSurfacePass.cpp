#include "pch.h"
#include "WaterSurfacePass.h"

#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Model/Model.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/RenderTarget/OffscreenRenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Scene/SceneManager.h"
#include "Graphics/Render/RenderGraph.h"
#include <cassert>

namespace CoreEngine
{
    void WaterSurfacePass::DeclareResources(RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        builder.Read(FrameBlackboard::SceneColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        builder.Read(FrameBlackboard::SceneDepth, D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        builder.Read(FrameBlackboard::SceneColorSnapshot, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        builder.Read(FrameBlackboard::RTWaterRefractionColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        builder.Write(FrameBlackboard::SceneColor, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }

    void WaterSurfacePass::ConfigureForView(const RenderContext& context)
    {
        SetRenderTargetName(context.viewSettings.sceneColorTargetName);
    }

    void WaterSurfacePass::Execute(const RenderContext& context)
    {
        if (!context.renderTargetManager) {
#ifdef _DEBUG
            OutputDebugStringA("ERROR: WaterSurfacePass: RenderTargetManager is null in RenderContext!\n");
#endif
            assert(false && "WaterSurfacePass requires RenderTargetManager in RenderContext");
            return;
        }

        RenderTarget* targetToUse = context.renderTargetManager->GetRenderTarget(targetName_);
        if (!targetToUse) {
#ifdef _DEBUG
            std::string msg = "ERROR: WaterSurfacePass: RenderTarget '" + targetName_ + "' not found in RenderTargetManager!\n";
            OutputDebugStringA(msg.c_str());
#endif
            assert(false && "WaterSurfacePass requires a valid RenderTarget.");
            return;
        }

        if (!context.dxCommon) {
#ifdef _DEBUG
            OutputDebugStringA("ERROR: WaterSurfacePass: DirectXCommon is null in RenderContext!\n");
#endif
            assert(false && "WaterSurfacePass requires DirectXCommon in RenderContext");
            return;
        }

        auto* cmdList = context.dxCommon->GetCommandList();
        targetToUse->SetClearEnabled(false);
        targetToUse->Begin(cmdList);
        if (context.renderManager) {
            context.renderManager->SetActiveTransformSlot(TransformBufferSlot::Game);
            context.renderManager->SetDebugLineRenderingEnabled(true);
            if (context.sceneManager) {
                context.renderManager->SetCamera(context.sceneManager->GetGameViewCamera3D());
            }
            Model::SetCurrentRenderSlot(TransformBufferSlot::Game);
            context.renderManager->DrawWaterQueuePass();
        }
        targetToUse->End(cmdList);

        // SceneColor は RegisterFrameResources で同一の実体が登録済みのため、
        // 実行中の Blackboard 再登録は行わない（パス分離契約 3）。
    }
}
