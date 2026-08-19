#pragma once

#include "Math/MathCore.h"

/// @file
/// @brief カメラ操作用に正規化した入力状態

namespace CoreEngine
{
    /// @brief カメラコントローラへ渡す入力（正規化済み）
    /// @details デバイス種別や ImGui / ギズモが入力を掴んでいるかの判断は収集側で済ませてある。
    ///          おかげでコントローラは ImGui / InputManager / WinApp に依存しない。
    struct CameraInputState {
        /// @brief 前フレームからのマウス移動量 [ピクセル]
        Vector2 mouseDelta{ 0.0f, 0.0f };

        /// @brief ホイールの生の移動量（DirectInput 単位。1ノッチ = 120）
        int wheelDelta = 0;

        // ===== ボタン（押下中かどうか） =====
        bool orbitButton = false;  ///< 中ドラッグ（軌道回転）
        bool panButton = false;    ///< Shift + 中ドラッグ（パン）
        bool lookButton = false;   ///< 右ドラッグ（一人称視点回転）

        // ===== 移動キー =====
        bool forward = false;
        bool back = false;
        bool left = false;
        bool right = false;
        bool up = false;
        bool down = false;
        bool boost = false;        ///< Shift（加速）

        /// @brief カメラ操作を受け付けてよい状態か
        /// @details ビューポート上にカーソルがあり、かつギズモ操作中でもドッキング操作中でもない。
        ///          false のときコントローラは一切状態を変えない。
        bool active = false;

        /// @brief 何も入力が無い状態を返す（エディタ非搭載ビルド・ゲーム実行中など）
        static CameraInputState None() { return CameraInputState{}; }
    };
}
