#pragma once

#include "RenderPass.h"

namespace CoreEngine
{
    /// @brief Hi-Z オクルージョンカリングパス
    /// @note G-Buffer 完成直後の SceneDepth から Hi-Z ピラミッドを構築し、
    ///       フレーム中に収集したワールド AABB を遮蔽判定して Readback する。
    ///       結果はリング一巡後（2 フレーム後）の Model::Draw が Submit スキップに使う。
    ///       メインの GameView のみで実行する（補助ビューはカメラが異なるため）。
    class HiZOcclusionPass : public RenderPass {
    public:
        HiZOcclusionPass() = default;
        ~HiZOcclusionPass() override = default;

        const char* GetName() const override { return "HiZOcclusion"; }
        void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) override;
        bool IsEnabledForView(const RenderViewSettings& view) const override;
        void Execute(const RenderContext& context) override;
    };
}
