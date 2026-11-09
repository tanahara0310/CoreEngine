#include "pch.h"
#include "RTDebugPass.h"

#ifdef CORE_EDITOR

#include "Graphics/RayTracing/Debug/RTDebugSystem.h"
#include "Graphics/Render/FrameBlackboard.h"
#include "Graphics/Render/RenderGraph.h"

namespace CoreEngine
{
    void RTDebugRayRecordPass::Execute(const RenderContext& context)
    {
        if (context.rtDebug) {
            context.rtDebug->BeginRayRecord(context);
        }
    }

    void RTDebugScenePass::DeclareResources(RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        builder.Read(FrameBlackboard::SceneDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    void RTDebugScenePass::Execute(const RenderContext& context)
    {
        if (context.rtDebug) {
            context.rtDebug->ExecuteScene(context);
        }
    }

    void RTDebugVisualizePass::Execute(const RenderContext& context)
    {
        if (context.rtDebug) {
            context.rtDebug->ExecuteVisualize(context);
        }
    }
}

#endif // CORE_EDITOR
