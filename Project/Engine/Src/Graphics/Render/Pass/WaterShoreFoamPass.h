#pragma once

#include "RenderPass.h"

namespace CoreEngine
{
    /// @brief 岸の泡を 1 フレーム進めるパス（海底の高さを測った後・水面の合成の前）
    class WaterShoreFoamPass : public RenderPass
    {
    public:
        const char* GetName() const override { return "WaterShoreFoamPass"; }

        /// @brief GameView のみ実行する（範囲はゲームのカメラの周り）
        bool IsEnabledForView(const RenderViewSettings& view) const override {
            return view.viewType == RenderViewType::GameView;
        }

        void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) override;
        void Execute(const RenderContext& context) override;
    };
}
