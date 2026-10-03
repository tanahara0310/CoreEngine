#pragma once

#include "RenderPass.h"

namespace CoreEngine
{
    /// @brief 水中の直接光を RT コースティクスで置き換える設定か
    /// @details 水面があり、コースティクスが有効でレイトレース方式を選んでいるとき true。
    ///          RT シャドウが水中の受光点を屈折した経路で調べるかの判定にも使う
    bool IsRayTracedWaterCausticsSelected(const RenderContext& context);

    /// @brief 水面のコースティクスをレイトレーシングで生成するパス
    class RTWaterCausticsPass : public RenderPass
    {
    public:
        const char* GetName() const override { return "RTWaterCausticsPass"; }

        /// @brief GameView のみ実行する
        bool IsEnabledForView(const RenderViewSettings& view) const override {
            return view.viewType == RenderViewType::GameView;
        }

        void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) override;
        void Execute(const RenderContext& context) override;
    };
}
