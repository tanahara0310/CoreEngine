#pragma once

#include "RenderPass.h"

namespace CoreEngine
{
    /// @brief G-Buffer から一時的なディファードライティングを生成するパス
    /// @note 現段階では移行用の簡易ライティング。最終版では LightManager / Shadow / IBL を統合予定。
    class DeferredLightingPass : public RenderPass {
    public:
        DeferredLightingPass() = default;
        ~DeferredLightingPass() override = default;

        const char* GetName() const override { return "DeferredLighting"; }
        void Execute(const RenderContext& context) override;

        void SetRenderTargetName(const std::string& name) { targetName_ = name; }

    private:
        std::string targetName_ = "Offscreen0";
    };
}
