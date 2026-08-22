#include "pch.h"
#include "GBufferPass.h"

#include <cassert>

#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/DepthStencilManager.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/RenderGraph.h"

namespace CoreEngine
{
    void GBufferPass::DeclareResources(RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        // 各 MRT と SceneDepth を書き込む Deferred の起点パス。
        builder.Write(FrameBlackboard::SceneDepth, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        builder.Write(FrameBlackboard::GBufferAlbedoAO, D3D12_RESOURCE_STATE_RENDER_TARGET);
        builder.Write(FrameBlackboard::GBufferNormalRoughness, D3D12_RESOURCE_STATE_RENDER_TARGET);
        builder.Write(FrameBlackboard::GBufferEmissiveMetallic, D3D12_RESOURCE_STATE_RENDER_TARGET);
        builder.Write(FrameBlackboard::GBufferMotionVector, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }

    void GBufferPass::Execute(const RenderContext& context)
    {
        if (!context.dxCommon || !context.renderManager || !context.gBufferManager) {
#ifdef _DEBUG
            OutputDebugStringA("WARNING: GBufferPass skipped because required context is missing.\n");
#endif
            return;
        }

        auto* cmdList = context.cmdList;
        auto* gBufferManager = context.gBufferManager;

        if (context.depthStencilManager) {
            context.depthStencilManager->BeginDepthWrite(cmdList);
        }

        // GBuffer の各 MRT と深度へ書き込むジオメトリパスを開始する。
        gBufferManager->BeginGeometryPass(
            cmdList,
            context.depthStencilManager,
            context.dxCommon->GetSRVHeap());

        // 不透明オブジェクトを GBuffer へ描画する。
        // ビュー種別は DrawViewInfo として各オブジェクト（Model）まで明示的に流れる。
        context.renderManager->DrawGBufferPass(cmdList, context.viewSettings.viewType);

        // GBuffer 各 MRT と SceneDepth は RegisterFrameResources で同一の実体が
        // 登録済みのため、実行中の Blackboard 再登録は行わない（パス分離契約 3）。

        // GBuffer の各バッファは context.gBufferManager 経由で後続パスが直接取得する。
    }
}
