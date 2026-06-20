#include "pch.h"
#include "SSAOPass.h"

#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueManager.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueNames.h"
#include "Graphics/Render/RenderingTechnique/SSAO/SSAOTechnique.h"
#include "Graphics/Render/RenderingTechnique/SSAO/SSAOBlurTechnique.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/OffscreenRenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Graphics/Render/RenderManager.h"

namespace CoreEngine
{
    void SSAOPass::Execute(const RenderContext& context)
    {
        if (!context.renderingTechniqueManager || !context.renderTargetManager
            || !context.gBufferManager || !context.dxCommon) {
            return;
        }

        auto* ssao = context.renderingTechniqueManager->GetTechnique<SSAOTechnique>(RenderingTechniqueNames::SSAO);
        auto* ssaoBlur = context.renderingTechniqueManager->GetTechnique<SSAOBlurTechnique>(RenderingTechniqueNames::SSAOBlur);

        if (!ssao || !ssao->IsEnabled()) {
            return;
        }

        // GBuffer と深度を使って SSAO を生成する。
        D3D12_GPU_DESCRIPTOR_HANDLE ssaoOutput{};
        ssao->Execute(context, ssaoOutput);

        // 有効ならブラーを適用して、より安定した AO を得る。
        if (ssaoBlur && ssaoBlur->IsEnabled() && ssaoOutput.ptr != 0) {
            D3D12_GPU_DESCRIPTOR_HANDLE blurOutput{};
            ssaoBlur->Execute(context, blurOutput);
            ssaoOutput = blurOutput;
        }

        if (context.frameBlackboard) {
            ID3D12Resource* ssaoResource = nullptr;
            if (ssaoOutput.ptr != 0) {
                const std::string& targetName = (ssaoBlur && ssaoBlur->IsEnabled() && ssaoOutput.ptr != 0)
                    ? ssaoBlurTargetName_
                    : ssaoTargetName_;
                if (RenderTarget* target = context.renderTargetManager->GetRenderTarget(targetName)) {
                    ssaoResource = target->GetResource();
                    if (auto* offscreen = dynamic_cast<OffscreenRenderTarget*>(target)) {
                        context.frameBlackboard->SetResource(
                            FrameBlackboard::SSAO,
                            ssaoOutput,
                            ssaoResource,
                            &offscreen->GetCurrentState());
                        return;
                    }
                }
            }

            context.frameBlackboard->SetResource(
                FrameBlackboard::SSAO,
                ssaoOutput,
                ssaoResource);
        }
    }
}
