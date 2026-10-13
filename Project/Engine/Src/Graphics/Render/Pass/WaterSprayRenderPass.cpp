#include "pch.h"
#include "WaterSprayRenderPass.h"

#include "Camera/View/ViewInfo.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include "Graphics/Render/RenderGraph.h"
#include "Graphics/Render/RenderTarget/OffscreenRenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Graphics/Water/Spray/WaterSpraySystem.h"
#include "Graphics/Water/WaterCVars.h"

namespace CoreEngine
{
    void WaterSprayRenderPass::ConfigureForView(const RenderContext& context)
    {
        targetName_ = context.viewSettings.sceneColorTargetName;
    }

    void WaterSprayRenderPass::DeclareResources(RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        builder.Read(FrameBlackboard::WaterSprayParticles, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Read(FrameBlackboard::SceneDepth, D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        builder.Write(FrameBlackboard::SceneColor, D3D12_RESOURCE_STATE_RENDER_TARGET);
        // 流体描画のしぶきは自分の動きをモーションベクターへ書く
        builder.Write(FrameBlackboard::GBufferMotionVector, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }

    void WaterSprayRenderPass::Execute(const RenderContext& context)
    {
        auto* spray = context.waterSpraySystem;
        if (!context.waterSurfaceState || context.waterSurfaceState->regionValid == 0
            || !spray || !spray->IsActive() || !spray->HasParticles() || !context.cmdList
            || !context.renderTargetManager || !context.frameViews || !context.frameBlackboard) {
            return;
        }
        const ViewInfo& view = context.frameViews->GameView();
        auto* target = dynamic_cast<OffscreenRenderTarget*>(context.renderTargetManager->GetRenderTarget(targetName_));
        if (!view.isValid || !target || !target->GetResource()) {
            return;
        }

        WaterSpraySystem::RenderInput input{};
        input.view = &view;
        input.sceneColor = &target->Resource();
        input.sceneColorRTV = target->GetRTVHandle();
        input.frameNumber = context.frameNumber;
        if (!context.frameBlackboard->TryGetSrvHandle(FrameBlackboard::SceneDepth, input.sceneDepthSRV)) {
            return;
        }
        if (context.gBufferManager && WaterCVars::WriteMotionVector.Get()) {
            constexpr auto kMotionVector = GBufferManager::Target::MotionVector;
            input.motionVector = &context.gBufferManager->Resource(kMotionVector);
            input.motionVectorRTV = context.gBufferManager->GetRTVHandle(kMotionVector);
            input.motionVectorFormat = GBufferManager::kRenderTargetFormats[static_cast<size_t>(kMotionVector)];
        }
        if (context.lightManager) {
            input.lightCountsCBV = context.lightManager->GetLightCountsGPUAddress();
            input.directionalLightsSRV = context.lightManager->GetDirectionalLightsSRVHandle();
        }
        spray->Render(context.cmdList, input);
    }
}
