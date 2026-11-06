#pragma once

#include "RenderPass.h"

namespace CoreEngine
{
    /// @brief 半透明の前に、画面の深度を写しへ写すパス（シェーダーグラフの Scene Depth が半透明で読む）
    /// @details 前のフレームで半透明が深度を読んだフレームだけ Graph に入る。
    class SceneDepthCopyPass : public RenderPass {
    public:
        SceneDepthCopyPass() = default;
        ~SceneDepthCopyPass() override = default;

        const char* GetName() const override { return "SceneDepthCopy"; }

        void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) override;

        /// @brief このフレームで写すかを深度から受け取る
        void ConfigureForView(const RenderContext& context) override;

        /// @brief 写すフレームだけ有効
        bool IsEnabledForView(const RenderViewSettings& view) const override;

        void Execute(const RenderContext& context) override;

    private:
        bool copyWanted_ = false;
    };
}
