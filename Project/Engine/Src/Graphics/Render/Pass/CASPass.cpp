#include "pch.h"
#include "CASPass.h"

#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/Render/RenderGraph.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueManager.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueNames.h"
#include "Graphics/Render/RenderingTechnique/CAS/CASTechnique.h"

namespace CoreEngine
{
    void CASPass::DeclareResources(RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        builder.Read(inputResourceName_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        builder.Write(FrameBlackboard::CASOutput, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }

    void CASPass::Execute(const RenderContext& context)
    {
        if (!context.renderingTechniqueManager) {
            return;
        }

        auto* cas = context.renderingTechniqueManager->GetTechnique<CASTechnique>(RenderingTechniqueNames::CAS);
        if (!cas || !cas->IsEnabled()) {
            return;
        }

        // Graph 構築時に決まった入力名をそのまま渡す（宣言した Read と実際の読み先を一致させる）
        cas->SetInputResourceName(inputResourceName_);

        D3D12_GPU_DESCRIPTOR_HANDLE casOutput{};
        cas->Execute(context, casOutput);
    }
}
