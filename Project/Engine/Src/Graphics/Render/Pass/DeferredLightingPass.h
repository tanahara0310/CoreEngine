#pragma once

#include "RenderPass.h"

namespace CoreEngine
{
    /// @brief G-Buffer 縺九ｉ荳譎ら噪縺ｪ繝・ぅ繝輔ぃ繝ｼ繝峨Λ繧､繝・ぅ繝ｳ繧ｰ繧堤函謌舌☆繧九ヱ繧ｹ
    /// @note 迴ｾ谿ｵ髫弱〒縺ｯ遘ｻ陦檎畑縺ｮ邁｡譏薙Λ繧､繝・ぅ繝ｳ繧ｰ縲よ怙邨ら沿縺ｧ縺ｯ LightManager / Shadow / IBL 繧堤ｵｱ蜷井ｺ亥ｮ壹・
    class DeferredLightingPass : public RenderPass {
    public:
        DeferredLightingPass() = default;
        ~DeferredLightingPass() override = default;

        const char* GetName() const override { return "DeferredLighting"; }
        void Execute(const RenderContext& context) override;
        void Setup(const RenderContext& context) override;

        void SetRenderTargetName(const std::string& name) { targetName_ = name; }

        /// @brief 蜑阪・繝代せ・・SAOPass 遲会ｼ峨・蜃ｺ蜉帙ｒ蜿励￠蜿悶ｋ
        void SetInput(const PassOutput& input) override { input_ = input; }

    private:
        std::string targetName_ = "Offscreen0";
        PassOutput  input_{};   ///< SSAOPass 縺ｮ蜃ｺ蜉幢ｼ・SAO SRV・・
    };
}
