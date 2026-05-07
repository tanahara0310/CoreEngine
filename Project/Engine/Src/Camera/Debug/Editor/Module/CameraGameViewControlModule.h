#pragma once

#ifdef USE_IMGUI

#include "ICameraEditorModule.h"

namespace CoreEngine
{
    /// @brief GameビューカメラをFPS風に自由操作するモジュール
    class CameraGameViewControlModule final : public ICameraEditorModule {
    public:
        /// @brief タブ名を取得
        const char* GetTabName() const override { return "Gameビュー操作"; }

        /// @brief 毎フレーム更新（キー入力によるカメラ移動）
        void Update(const CameraEditorContext& context) override;

        /// @brief タブ内容を描画
        void Draw(const CameraEditorContext& context) override;

    private:
        bool  flightEnabled_  = false;  // フライトモード有効フラグ
        float moveSpeed_      = 10.0f;  // WASD移動速度 (m/s)
        float lookSensitivity_= 0.003f; // 右ドラッグ回転感度
        bool  invertY_        = false;  // Y軸反転

        // 右ドラッグ状態
        bool  draggingRight_  = false;

        // リリースカメラのpitch/yaw（Camera は rotate_.x/y で保持）
        // ここでは Camera::rotate_ を直接編集するため別管理不要
    };
}

#endif // USE_IMGUI
