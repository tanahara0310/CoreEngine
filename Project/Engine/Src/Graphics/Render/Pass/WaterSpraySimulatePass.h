#pragma once

#include "RenderPass.h"

namespace CoreEngine
{
    /// @brief 岸の波のしぶきを出して動かすパス（岸の波の場を作った後・水面の前）
    class WaterSpraySimulatePass : public RenderPass
    {
    public:
        const char* GetName() const override { return "WaterSpraySimulatePass"; }

        /// @brief GameView のみ実行する（範囲はゲームのカメラの周り）
        bool IsEnabledForView(const RenderViewSettings& view) const override {
            return view.viewType == RenderViewType::GameView;
        }

        void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) override;
        void Execute(const RenderContext& context) override;

    private:
        /// @brief 前のフレームの波の時計 [s]
        float lastSimulationTime_ = 0.0f;
        bool hasLastSimulationTime_ = false;
    };
}
