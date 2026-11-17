#pragma once

#ifdef CORE_EDITOR

#include <string>

namespace CoreEngine::Editor
{
    struct AnimatorDocument;

    /// @brief アニメーターの窓の右（選んだ状態・どこからでも・遷移の設定。何も選んでいなければ設定全体）
    class AnimatorInspectorPanel
    {
    public:
        void Draw(AnimatorDocument& document);

    private:
        void DrawControllerInspector(AnimatorDocument& document);
        void DrawStateInspector(AnimatorDocument& document, int stateIndex);
        void DrawAnyStateInspector(AnimatorDocument& document);
        void DrawTransitionInspector(AnimatorDocument& document, int transitionIndex);

        /// @brief 元の状態から出る遷移を、調べる順に並べる（上下の矢印で順を変える）
        void DrawTransitionList(AnimatorDocument& document, const std::string& from);
    };
}

#endif // CORE_EDITOR
