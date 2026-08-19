#pragma once

#include "Camera/Control/ICameraController.h"
#include "Math/MathCore.h"

/// @file
/// @brief 一人称視点の自由移動コントローラ（右ドラッグで視点回転 + WASD 移動）

namespace CoreEngine
{
    class Camera;

    /// @brief 自由移動（フライ）コントローラ
    /// @details 注視点を持たず、カメラの位置と向きを直接動かす。
    ///          以前は CameraGameViewControlModule（エディタ UI）が入力処理まで抱えており、
    ///          対象カメラの選び方も「最初に見つかった Camera 型」と不定だった。
    ///          操作をコントローラとして切り出したことで、UI 側は設定の表示だけになる。
    class FreeLookController final : public ICameraController {
    public:
        /// @brief 操作感の設定
        struct Settings {
            float moveSpeed = 10.0f;          ///< 移動速度 [m/s]
            float boostMultiplier = 4.0f;     ///< Shift 押下時の倍率
            float lookSensitivity = 0.003f;   ///< 右ドラッグの回転感度
            bool invertY = false;
        };

    public:
        FreeLookController() = default;

        // ===== ICameraController =====
        void Update(const CameraInputState& input, float deltaTime, Camera& camera) override;
        void ApplyTo(Camera& camera) const override;
        void Reset() override;
        const char* GetDisplayName() const override { return "自由移動 (フライ)"; }

        const Settings& GetSettings() const { return settings_; }
        void SetSettings(const Settings& settings) { settings_ = settings; }

        /// @brief 視点回転のドラッグ中かどうか
        bool IsControlling() const { return looking_; }

        /// @brief 操作の有効/無効
        /// @details 既定は無効。ゲーム用カメラへ付けたまま、エディタで必要なときだけ
        ///          有効化する運用（従来の「フライトモード」トグルと同じ既定）。
        void SetEnabled(bool enabled);
        bool IsEnabled() const { return enabled_; }

    private:
        Settings settings_{};
        bool enabled_ = false;
        // ドラッグ開始は押し込みエッジでのみ判定する（押しっぱなしでの誤作動防止）
        bool looking_ = false;
        bool prevLookButton_ = false;
    };
}
