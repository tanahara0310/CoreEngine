#pragma once

#include "RenderPass.h"
#include "Graphics/Render/RenderTarget/RenderTargetNames.h"
#include <string>

namespace CoreEngine
{
    /// @brief 水面専用の forward 合成パス
    class WaterSurfacePass : public RenderPass {
    public:
        WaterSurfacePass() = default;
        ~WaterSurfacePass() override = default;

        const char* GetName() const override { return "WaterSurface"; }

        void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) override;

        /// @brief View の SceneColor ターゲット名を出力先へ反映する
        void ConfigureForView(const RenderContext& context) override;

        void Execute(const RenderContext& context) override;

        void SetRenderTargetName(const std::string& name) {
            targetName_ = name;
        }

        const std::string& GetRenderTargetName() const {
            return targetName_;
        }

    private:
        std::string targetName_ = RenderTargetNames::SceneColor;
    };
}
