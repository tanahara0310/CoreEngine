#pragma once

#ifdef CORE_EDITOR

#include <string>

namespace CoreEngine::Editor
{
    struct AnimatorDocument;
    struct AnimatorLiveView;

    /// @brief アニメーターの窓の右（選んだ状態・どこからでも・遷移の設定。何も選んでいなければ設定全体）
    class AnimatorInspectorPanel
    {
    public:
        void Draw(AnimatorDocument& document, const AnimatorLiveView& live);

    private:
        void DrawControllerInspector(AnimatorDocument& document);
        void DrawStateInspector(AnimatorDocument& document, int stateIndex, const AnimatorLiveView& live);
        void DrawAnyStateInspector(AnimatorDocument& document);
        void DrawTransitionInspector(AnimatorDocument& document, int transitionIndex);

        /// @brief クリップ 1 本の状態のクリップの欄
        void DrawClipMotion(AnimatorDocument& document, int stateIndex, const AnimatorLiveView& live);

        /// @brief ブレンドツリーの状態のパラメータ・図・子の表・子のクリップの欄
        void DrawBlendTree(AnimatorDocument& document, int stateIndex, const AnimatorLiveView& live);

        /// @brief 元の状態から出る遷移を、調べる順に並べる（上下の矢印で順を変える）
        void DrawTransitionList(AnimatorDocument& document, const std::string& from);
    };
}

#endif // CORE_EDITOR
