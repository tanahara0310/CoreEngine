#pragma once

#include "RenderPass.h"

namespace CoreEngine
{
    /// @brief G-Buffer蓄積パス
    /// @note 不透明な Model / SkinnedModel を MRT に書き込む。
    ///       LightingPass 導入完了までは従来の GeometryPass も並行使用する。
    class GBufferPass : public RenderPass {
    public:
        GBufferPass() = default;
        ~GBufferPass() override = default;

        const char* GetName() const override { return "GBuffer"; }
        void Execute(const RenderContext& context) override;
    };
}
