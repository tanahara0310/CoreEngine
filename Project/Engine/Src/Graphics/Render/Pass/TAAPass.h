#pragma once
#include "RenderPass.h"

namespace CoreEngine
{
    /// @brief TAA（テンポラルアンチエイリアス）解決パス
    /// @details トーンマップ前の HDR 空間で解決するため PostProcess フェーズの先頭に置く。
    /// @note 出力は履歴 ping-pong の片側。SceneColor へは書き戻さず、
    ///       後続の入力名を RenderPipeline が TAAOutput へ差し替える。
    class TAAPass : public RenderPass {
    public:
        TAAPass() = default;
        ~TAAPass() override = default;

        const char* GetName() const override { return "TAAPass"; }
        void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) override;

        /// @brief GameView のみ実行する
        /// @details 履歴もモーションベクターも GameView 専用（Model の prevWVP が GameView 基準）。
        bool IsEnabledForView(const RenderViewSettings& view) const override {
            return view.viewType == RenderViewType::GameView;
        }

        void Execute(const RenderContext& context) override;
    };
}
