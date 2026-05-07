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

        /// @brief 前のパス（SSAOPass 等）の出力を受け取る
        void SetInput(const PassOutput& input) override { input_ = input; }

    private:
        std::string targetName_ = "Offscreen0";
        PassOutput  input_{};   ///< SSAOPass の出力（SSAO SRV）
    };
}
