#pragma once

#include "RenderPass.h"
#include "Graphics/Render/RenderTarget/RenderTargetNames.h"
#include <string>

namespace CoreEngine
{
    /// @brief SceneColor を SceneColorSnapshot へ複製するパス
    class SceneColorCopyPass : public RenderPass {
    public:
        SceneColorCopyPass() = default;
        ~SceneColorCopyPass() override = default;

        const char* GetName() const override { return "SceneColorCopy"; }

        void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) override;

        /// @brief コピー元 / コピー先を View 設定へ追従させる
        void ConfigureForView(const RenderContext& context) override;

        /// @brief GameView かつ標準 SceneColor ターゲット時のみ有効
        bool IsEnabledForView(const RenderViewSettings& view) const override;

        void Execute(const RenderContext& context) override;

        void SetSourceTargetName(const std::string& name) { sourceTargetName_ = name; }
        const std::string& GetSourceTargetName() const { return sourceTargetName_; }

        void SetDestinationTargetName(const std::string& name) { destinationTargetName_ = name; }
        const std::string& GetDestinationTargetName() const { return destinationTargetName_; }

    private:
        std::string sourceTargetName_ = RenderTargetNames::SceneColor;
        std::string destinationTargetName_ = RenderTargetNames::SceneColorSnapshot;
    };
}
