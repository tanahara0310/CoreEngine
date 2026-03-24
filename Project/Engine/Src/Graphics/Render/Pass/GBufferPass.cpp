#include "GBufferPass.h"

#include <cassert>

#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include "Graphics/Render/RenderManager.h"

namespace CoreEngine
{
    void GBufferPass::Execute(const RenderContext& context)
    {
        if (!context.dxCommon || !context.renderManager || !context.gBufferManager) {
#ifdef _DEBUG
            OutputDebugStringA("WARNING: GBufferPass skipped because required context is missing.\n");
#endif
            output_.Reset();
            return;
        }

        auto* cmdList = context.dxCommon->GetCommandList();
        auto* gBufferManager = context.gBufferManager;

        gBufferManager->BeginGeometryPass(
            cmdList,
            context.dxCommon->GetDSVHandle(),
            context.dxCommon->GetSRVHeap(),
            true);

        context.renderManager->DrawGBufferPass();
        gBufferManager->EndGeometryPass(cmdList);

        output_.srvHandle = gBufferManager->GetSRVHandle(GBufferManager::Target::AlbedoAO);
        output_.resource = gBufferManager->GetResource(GBufferManager::Target::AlbedoAO);
        output_.isValid = true;
    }
}
