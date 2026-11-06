#include "pch.h"
#include "SceneDepthCopyPass.h"

#include "Graphics/Render/RenderGraph.h"
#include "Graphics/Render/RenderTarget/SceneDepth.h"

namespace CoreEngine
{
    void SceneDepthCopyPass::DeclareResources(RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        builder.Read(FrameBlackboard::SceneDepth, D3D12_RESOURCE_STATE_COPY_SOURCE);
        builder.Write(FrameBlackboard::SceneDepthCopy, D3D12_RESOURCE_STATE_COPY_DEST);
    }

    void SceneDepthCopyPass::ConfigureForView(const RenderContext& context)
    {
        copyWanted_ = context.sceneDepth && context.sceneDepth->IsCopyWanted();
    }

    bool SceneDepthCopyPass::IsEnabledForView([[maybe_unused]] const RenderViewSettings& view) const
    {
        return copyWanted_;
    }

    void SceneDepthCopyPass::Execute(const RenderContext& context)
    {
        if (!context.sceneDepth || !context.cmdList) {
            return;
        }
        context.sceneDepth->CopyForShaderRead(context.cmdList);
    }
}
