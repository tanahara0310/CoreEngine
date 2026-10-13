#pragma once

#include "RenderPass.h"
#include "Graphics/Render/RenderTarget/RenderTargetNames.h"

#include <string>

namespace CoreEngine
{
    /// @brief 岸の波のしぶきをシーンの色へ描くパス（水面を描いた後・TAA の前）
    class WaterSprayRenderPass : public RenderPass
    {
    public:
        const char* GetName() const override { return "WaterSprayRenderPass"; }

        /// @brief GameView のみ実行する
        bool IsEnabledForView(const RenderViewSettings& view) const override {
            return view.viewType == RenderViewType::GameView;
        }

        /// @brief View の SceneColor ターゲット名を描画先へ反映する
        void ConfigureForView(const RenderContext& context) override;

        void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) override;
        void Execute(const RenderContext& context) override;

    private:
        std::string targetName_ = RenderTargetNames::SceneColor;
    };
}
