#pragma once

#ifdef CORE_EDITOR

#include "RenderPass.h"

namespace CoreEngine
{
    /// @brief レイトレーシングのデバッグ表示：RT から見たシーンと 1 画素のレイの追跡（エディタ版だけ）
    /// @details デバッグ窓の「見え方」「レイの追跡」タブが頼んだフレームだけ撃つ。TLAS とシーンの深度を読む
    class RTDebugScenePass : public RenderPass {
    public:
        const char* GetName() const override { return "RTDebugScene"; }

        /// @brief GameView のみ実行する
        bool IsEnabledForView(const RenderViewSettings& view) const override {
            return view.viewType == RenderViewType::GameView;
        }

        void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) override;
        void Execute(const RenderContext& context) override;
    };

    /// @brief レイトレーシングのデバッグ表示：中間結果に色を付けて数える（エディタ版だけ）
    /// @details 全部の効果とポストエフェクトの後に置く。窓が中間結果を見ているフレームだけ積む
    class RTDebugVisualizePass : public RenderPass {
    public:
        const char* GetName() const override { return "RTDebugVisualize"; }

        /// @brief GameView のみ実行する
        bool IsEnabledForView(const RenderViewSettings& view) const override {
            return view.viewType == RenderViewType::GameView;
        }

        void Execute(const RenderContext& context) override;
    };
}

#endif // CORE_EDITOR
