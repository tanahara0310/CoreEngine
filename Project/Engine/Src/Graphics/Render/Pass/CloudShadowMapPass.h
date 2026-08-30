#pragma once

#include "Graphics/Render/Pass/RenderPass.h"

namespace CoreEngine
{
    /// @brief 太陽方向の雲透過率マップを生成する
    /// @note Deferred ライティングより前のフェーズで走る。ゴッドレイも同じマップを読む。
    class CloudShadowMapPass : public RenderPass {
    public:
        const char* GetName() const override { return "CloudShadowMap"; }
        void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) override;
        void Execute(const RenderContext& context) override;
    };
}
