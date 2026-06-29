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
