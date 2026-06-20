#pragma once
#include "RenderPass.h"
#include "Graphics/Render/RenderTarget/RenderTargetNames.h"
#include <string>

namespace CoreEngine
{
    /// @brief SSAO生成 + バイラテラルブラーを実行するパス
    /// @details GBufferPass の後、DeferredLightingPass の前に挿入する。
    ///          結果は RenderTargetNames::SSAOBuffer に書き込まれ、
    ///          DeferredLightingPass が AO テクスチャとして参照する。
    class SSAOPass : public RenderPass {
    public:
        SSAOPass() = default;
        ~SSAOPass() override = default;

        const char* GetName() const override { return "SSAOPass"; }
        void Execute(const RenderContext& context) override;

        void SetSSAOTargetName(const std::string& name) { ssaoTargetName_ = name; }
        void SetSSAOBlurTargetName(const std::string& name) { ssaoBlurTargetName_ = name; }

    private:
        std::string ssaoTargetName_     = RenderTargetNames::SSAOBuffer;
        std::string ssaoBlurTargetName_ = RenderTargetNames::SSAOBlurBuffer;
    };
}
