#pragma once

#include "RenderPass.h"

namespace CoreEngine
{
    /// @brief 水面の範囲全体の水深と陸をレイトレーシングで測り、波を弱める地図（海の範囲マップ）を作るパス
    /// @details 測るのは条件が変わったときだけ。できた地図は WaterRenderFeature が次のフレームから水面へ渡す
    class RTWaterSeaMapPass : public RenderPass
    {
    public:
        const char* GetName() const override { return "RTWaterSeaMapPass"; }

        /// @brief GameView のみ実行する（地図はビューに依らず 1 枚）
        bool IsEnabledForView(const RenderViewSettings& view) const override {
            return view.viewType == RenderViewType::GameView;
        }

        void Execute(const RenderContext& context) override;
    };
}
