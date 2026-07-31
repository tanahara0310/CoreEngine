#pragma once

#include "Camera/Control/CameraInputState.h"

/// @brief エディタのビューポート入力を CameraInputState へ正規化する

namespace CoreEngine
{
    class EngineSystem;

    /// @brief カメラ操作用の入力収集
    /// @details ImGui / ImGuizmo / InputManager / ウィンドウ判定への依存はここだけに閉じる。
    ///          コントローラ側は正規化された CameraInputState しか見ないため、
    ///          エディタ非搭載ビルドでも同じコードパスが動く。
    class EditorCameraInput {
    public:
        /// @brief 今フレームの入力を収集する
        /// @param engine 入力取得に使うエンジン（nullptr 可）
        /// @return 正規化済み入力。エディタ非搭載ビルドでは常に「入力なし」
        static CameraInputState Collect(EngineSystem* engine);

    private:
        // 前フレームのカーソル位置（フレーム間の移動量を出すため）
        static inline float lastCursorX_ = 0.0f;
        static inline float lastCursorY_ = 0.0f;
        static inline bool hasLastCursor_ = false;
    };
}
