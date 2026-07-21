#include "pch.h"
#include "GBufferPass.h"

#include <cassert>

#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Common/Core/DepthStencilManager.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/RenderGraph.h"
#include "Graphics/Render/Model/BaseModelRenderer.h"

namespace CoreEngine
{
    void GBufferPass::DeclareResources(RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        // 各 MRT と SceneDepth を書き込む Deferred の起点パス。
        builder.Write(FrameBlackboard::SceneDepth, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        builder.Write(FrameBlackboard::GBufferAlbedoAO, D3D12_RESOURCE_STATE_RENDER_TARGET);
        builder.Write(FrameBlackboard::GBufferNormalRoughness, D3D12_RESOURCE_STATE_RENDER_TARGET);
        builder.Write(FrameBlackboard::GBufferEmissiveMetallic, D3D12_RESOURCE_STATE_RENDER_TARGET);
        builder.Write(FrameBlackboard::GBufferWorldPosition, D3D12_RESOURCE_STATE_RENDER_TARGET);
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

        auto* cmdList = context.dxCommon->GetCommandList();
        auto* gBufferManager = context.gBufferManager;

        if (context.depthStencilManager) {
            context.depthStencilManager->BeginDepthWrite(cmdList);
        }

        // GBuffer の各 MRT と深度へ書き込むジオメトリパスを開始する。
        gBufferManager->BeginGeometryPass(
            cmdList,
            context.depthStencilManager,
            context.dxCommon->GetSRVHeap());

        // 反射ビューは半解像度かつ水面の歪みで細部が見えないため、LOD を 1 段落とす。
        // パス分離契約 2 に従い、ビュー依存の状態は毎回このパス自身が設定する。
        const uint32_t lodBias =
            (context.viewSettings.viewType == RenderViewType::ReflectionView) ? 1u : 0u;
        for (auto passType : { RenderPassType::Model, RenderPassType::SkinnedModel }) {
            if (auto* renderer = dynamic_cast<BaseModelRenderer*>(
                    context.renderManager->GetRenderer(passType))) {
                renderer->SetLodBias(lodBias);
            }
        }

        // 不透明オブジェクトを GBuffer へ描画する。
        context.renderManager->DrawGBufferPass();

        // GBuffer 各 MRT と SceneDepth は RegisterFrameResources で同一の実体が
        // 登録済みのため、実行中の Blackboard 再登録は行わない（パス分離契約 3）。

        // GBuffer の各バッファは context.gBufferManager 経由で後続パスが直接取得する。
    }
}
