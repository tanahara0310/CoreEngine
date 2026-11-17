#pragma once

#ifdef CORE_EDITOR

#include "AnimatorDocument.h"

#include <imgui.h>

namespace CoreEngine::Editor
{
    struct AnimatorLiveView;

    /// @brief アニメーターの窓の真ん中（状態の箱と遷移の矢印を並べ、マウスで作る・つなぐ・動かす）
    /// @details 見え方（位置・拡大）と選んでいるものは設定ごとに AnimatorDocument が持つ。
    class AnimatorCanvas
    {
    public:
        /// @brief 今の子ウィンドウいっぱいにキャンバスを描き、マウスとキーの操作を受ける
        void Draw(AnimatorDocument& document, const AnimatorLiveView& live);

    private:
        void DrawMenu(AnimatorDocument& document);

        // ===== 座標と当たり =====
        ImVec2 ToScreen(const AnimatorDocument& document, const ImVec2& canvasMin, const Vector2& world) const;
        Vector2 ToWorld(const AnimatorDocument& document, const ImVec2& canvasMin, const ImVec2& screen) const;
        Vector2 NodePosition(const AnimatorDocument& document, AnimatorSelection kind, int index) const;
        AnimatorHit HitTest(const AnimatorDocument& document, const ImVec2& canvasMin, const ImVec2& mouse) const;

        /// @brief 遷移の線の両端（画面の座標。描けなければ false）
        bool TransitionSegment(const AnimatorDocument& document, const ImVec2& canvasMin, int transitionIndex,
            ImVec2& start, ImVec2& end) const;

        /// @brief 全部の箱が収まるように位置と拡大を決める
        void FrameAll(AnimatorDocument& document, const ImVec2& canvasSize) const;
    };
}

#endif // CORE_EDITOR
