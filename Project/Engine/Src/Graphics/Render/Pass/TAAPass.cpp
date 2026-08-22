#include "pch.h"
#include "TAAPass.h"

#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/Render/RenderGraph.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueManager.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueNames.h"
#include "Graphics/Render/RenderingTechnique/TAA/TAATechnique.h"

namespace CoreEngine
{
    void TAAPass::DeclareResources(RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        // 現フレームの画・モーションベクター・前フレームの履歴を読み、今フレームの履歴へ書く。
        builder.Read(FrameBlackboard::SceneColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        builder.Read(FrameBlackboard::GBufferMotionVector, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        builder.Read(FrameBlackboard::TAAHistory, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        builder.Write(FrameBlackboard::TAAOutput, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }

    void TAAPass::Execute(const RenderContext& context)
    {
        if (!context.renderingTechniqueManager) {
            return;
        }

        auto* taa = context.renderingTechniqueManager->GetTechnique<TAATechnique>(RenderingTechniqueNames::TAA);
        if (!taa || !taa->IsEnabled()) {
            return;
        }

        D3D12_GPU_DESCRIPTOR_HANDLE taaOutput{};
        taa->Execute(context, taaOutput);
    }
}
