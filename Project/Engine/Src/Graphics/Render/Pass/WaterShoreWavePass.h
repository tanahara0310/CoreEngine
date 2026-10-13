#pragma once

#include "RenderPass.h"

namespace CoreEngine
{
    /// @brief 岸の波の場を作るパス（海底の高さを測った後・岸の泡と水面の前）
    class WaterShoreWavePass : public RenderPass
    {
    public:
        const char* GetName() const override { return "WaterShoreWavePass"; }

        /// @brief GameView のみ実行する（範囲はゲームのカメラの周り）
        bool IsEnabledForView(const RenderViewSettings& view) const override {
            return view.viewType == RenderViewType::GameView;
        }

        void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) override;
        void Execute(const RenderContext& context) override;
    };
}
