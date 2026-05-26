#include "pch.h"
#include "SSAOPass.h"

#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueManager.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueNames.h"
#include "Graphics/Render/RenderingTechnique/SSAO/SSAOTechnique.h"
#include "Graphics/Render/RenderingTechnique/SSAO/SSAOBlurTechnique.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Graphics/Render/RenderManager.h"

namespace CoreEngine
{
    void SSAOPass::Execute(const RenderContext& context)
    {
        if (!context.renderingTechniqueManager || !context.renderTargetManager
            || !context.gBufferManager || !context.dxCommon) {
            output_.Reset();
            return;
        }

        auto* ssao = context.renderingTechniqueManager->GetTechnique<SSAOTechnique>(RenderingTechniqueNames::SSAO);
        auto* ssaoBlur = context.renderingTechniqueManager->GetTechnique<SSAOBlurTechnique>(RenderingTechniqueNames::SSAOBlur);

        if (!ssao || !ssao->IsEnabled()) {
            output_.Reset();
            return;
        }

        // SSAO実行
        D3D12_GPU_DESCRIPTOR_HANDLE ssaoOutput{};
        ssao->Execute(context, ssaoOutput);

        // SSAOBlur実行
        if (ssaoBlur && ssaoBlur->IsEnabled() && ssaoOutput.ptr != 0) {
            D3D12_GPU_DESCRIPTOR_HANDLE blurOutput{};
            ssaoBlur->Execute(context, blurOutput);

            output_.srvHandle = blurOutput;
            output_.isValid = (blurOutput.ptr != 0);
        } else {
            output_.srvHandle = ssaoOutput;
            output_.isValid = (ssaoOutput.ptr != 0);
        }
    }
}
