#pragma once
#include "RenderPass.h"
#include <d3d12.h>

namespace CoreEngine
{
    /// @brief ポストエフェクト適用パス
    class PostEffectPass : public RenderPass {
    public:
        PostEffectPass() = default;
        ~PostEffectPass() override = default;

        const char* GetName() const override { return "PostEffect"; }

        void Execute(const RenderContext& context) override;
    };
}
