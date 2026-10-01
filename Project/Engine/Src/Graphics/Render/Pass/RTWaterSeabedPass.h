#pragma once

#include "RenderPass.h"

namespace CoreEngine
{
    /// @brief カメラの周りの範囲の海底の高さをレイトレーシングで測るパス
    /// @details 範囲は WaterRenderFeature がフレームの頭で決める。水面の岸の泡が読む
    class RTWaterSeabedPass : public RenderPass
    {
    public:
        const char* GetName() const override { return "RTWaterSeabedPass"; }

        /// @brief GameView のみ実行する（範囲はゲームのカメラの周り）
        bool IsEnabledForView(const RenderViewSettings& view) const override {
            return view.viewType == RenderViewType::GameView;
        }

        void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) override;
        void Execute(const RenderContext& context) override;
    };
}
