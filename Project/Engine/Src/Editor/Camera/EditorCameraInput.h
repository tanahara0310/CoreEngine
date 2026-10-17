#pragma once

#ifdef CORE_EDITOR

#include "Camera/Control/CameraInputState.h"

/// @file
/// @brief エディタのビューポート入力を CameraInputState へ正規化する

namespace CoreEngine
{
    class EngineSystem;

    /// @brief カメラ操作用の入力収集
    /// @details ImGui / ImGuizmo / InputManager / ウィンドウ判定への依存はここだけに閉じる。
    ///          コントローラ側は正規化された CameraInputState しか見ない。
    ///          エディタを含まないビルドにはこのクラスが無く、呼び出し側が「入力なし」を渡す。
    class EditorCameraInput {
    public:
        /// @brief 今フレームの入力を収集する
        /// @param engine 入力取得に使うエンジン（nullptr 可）
        /// @return 正規化済み入力
        static CameraInputState Collect(EngineSystem* engine);

        /// @brief Game 窓の中で映像を表示している範囲を教える（ImGui の座標）
        static void SetViewportRect(const Vector2& origin, const Vector2& size);

    private:
        // 映像を表示している範囲（前のフレームに描いた位置）
        static inline Vector2 viewportOrigin_{ 0.0f, 0.0f };
        static inline Vector2 viewportSize_{ 0.0f, 0.0f };

        // 前フレームのカーソル位置（フレーム間の移動量を出すため）
        static inline float lastCursorX_ = 0.0f;
        static inline float lastCursorY_ = 0.0f;
        static inline bool hasLastCursor_ = false;
    };
}

#endif // CORE_EDITOR
