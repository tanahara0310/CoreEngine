#include "pch.h"
#include "HiZOcclusionPass.h"

#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/Render/Culling/HiZOcclusionSystem.h"
#include "Graphics/Render/FrameBlackboard.h"
#include "Graphics/Render/RenderGraph.h"

namespace CoreEngine
{
    void HiZOcclusionPass::DeclareResources(
        RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        // G-Buffer が書き終えた深度を CS で読む（この宣言で GBufferPass 後に実行順が確定し、
        // DEPTH_WRITE → NON_PIXEL_SHADER_RESOURCE の遷移も RenderGraph が発行する）
        builder.Read(FrameBlackboard::SceneDepth,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    bool HiZOcclusionPass::IsEnabledForView(const RenderViewSettings& view) const
    {
        // メイン GameView のみ（viewName が空 = メイン。補助ビュー・反射・キャプチャは対象外）
        return view.viewType == RenderViewType::GameView && view.viewName.empty();
    }

    void HiZOcclusionPass::Execute(const RenderContext& context)
    {
        if (!system_ || !context.dxCommon) {
            return;
        }
        ID3D12GraphicsCommandList* cmdList = context.cmdList;
        if (!cmdList) {
            return;
        }

        // 深度は DeclareResources で Read 宣言した Blackboard の SceneDepth から取る
        // （GraphicsCore から直接読むと RenderGraph から見えない依存になる）
        const FrameBlackboardResource* depth = context.frameBlackboard
            ? context.frameBlackboard->GetResource(FrameBlackboard::SceneDepth)
            : nullptr;
        if (!depth || !depth->isValid || !depth->resource) {
            return;
        }

        system_->ExecuteCulling(
            cmdList,
            context.dxCommon,
            depth->resource->Get(),
            depth->srvHandle);
    }
}
