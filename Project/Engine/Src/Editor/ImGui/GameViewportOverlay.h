#pragma once

#ifdef CORE_EDITOR

#include <cstdint>
#include <functional>

#include <imgui.h>

namespace CoreEngine::Editor
{
    /// @brief ゲーム画面の画像の位置（重ねて描く側へ渡す）
    struct GameViewportOverlayArgs {
        ImVec2 imageMin{};     ///< ゲーム画像の左上（画面の座標）
        ImVec2 imageMax{};     ///< ゲーム画像の右下
        bool hovered = false;  ///< ゲーム画像の上にマウスがあるか
    };

    /// @brief ゲーム画面の上に重ねて描く関数
    /// @return true ならこのフレームのゲーム画面のクリックを使った（物の選択とギズモへ渡さない）
    using GameViewportOverlayFunction = std::function<bool(const GameViewportOverlayArgs&)>;

    /// @brief ゲーム画面の画像の上に重ねて描く口（デバッグ窓が中間結果を重ねる・画素を選ぶのに使う）
    /// @details 描く側は ImGui の項目を作らず、ウィンドウの描画リストへ描くこと（ゲーム画像の当たり判定が変わらないように）
    class GameViewportOverlay {
    public:
        /// @brief 足す（戻り値は Remove に渡す番号。0 にはならない）
        static uint32_t Add(GameViewportOverlayFunction function);
        /// @brief 外す
        static void Remove(uint32_t id);
        /// @brief 足した順に全部を描く（ゲーム画像を描いた直後に呼ぶ）
        /// @return どれかがクリックを使ったか
        static bool DrawAll(const GameViewportOverlayArgs& args);
    };
}

#endif // CORE_EDITOR
