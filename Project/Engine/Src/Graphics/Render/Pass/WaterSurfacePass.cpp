#include "pch.h"
#include "WaterSurfacePass.h"

#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Model/Model.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/RenderTarget/OffscreenRenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Scene/SceneManager.h"
#include <cassert>

namespace CoreEngine
{
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
