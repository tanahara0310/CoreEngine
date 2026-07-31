#pragma once

#include "Camera/Control/ICameraController.h"
#include "Math/MathCore.h"
#include <numbers>

/// @brief 注視点まわりの軌道操作 + 自由飛行でカメラを動かすコントローラ（Blender 風）

namespace CoreEngine
{
    class Camera;

    /// @brief 軌道 + 自由飛行のカメラコントローラ
    /// @details 注視点(target)・距離・ピッチ・ヨーはカメラの状態ではなくこのコントローラの
    ///          内部状態で、毎フレーム Camera の Transform（位置・回転）へ書き出す。
    ///          入力は CameraInputState 経由でのみ受け取り、ImGui / InputManager には依存しない。
    class OrbitFlyController final : public ICameraController {
    public:
        /// @brief 操作設定
        struct Settings {
            float rotationSensitivity = 0.003f;  // マウス回転感度
            // パン操作感度（ワールド単位/ピクセル）。distance に比例させると
            // 注視点へ寄るほどパンが実質止まって見えるため固定値にする。
            float panSensitivity = 0.05f;
            // ズーム1ノッチあたりの視線方向ドリー移動量 [m]。
            // distance に比例させないことで、寄っても頭打ちにならず遠くでも巨大化しない。
            float zoomSensitivity = 2.0f;
            float minDistance = 0.1f;
            float maxDistance = 10000.0f;
            bool invertY = false;
            bool smoothMovement = true;
            float smoothingFactor = 0.2f;

            // ===== WASD 自由移動（distance/target の束縛を受けないワールド固定速度） =====
            float flySpeed = 10.0f;       // 基本速度 [m/s]
            float flySpeedBoost = 4.0f;   // Shift 押下時の速度倍率

            // ===== 移動範囲の制限 =====
            // 注視点（= 回転中心）を収める範囲。ズーム・パン・自由移動はすべて target を
            // 動かすため、ここでクランプしないとカメラがどこまでも飛んでいく。
            float maxHorizontalExtent = 5000.0f; // 水平(X/Z)の移動限界 [m]
            float minHeight = -100.0f;           // 注視点の最低高度 [m]
            float maxHeight = 10000.0f;          // 注視点の最高高度 [m]
        };

        /// @brief 軌道状態（保存・復元の対象）
        struct OrbitState {
            Vector3 target = { 0.0f, 0.0f, 0.0f };
            float distance = 20.0f;
            float pitch = 0.25f;
            float yaw = std::numbers::pi_v<float>;
        };

        /// @brief 視点プリセット
        enum class Preset {
            Default, Front, Back, Left, Right, Top, Bottom, Diagonal, CloseUp, Wide
        };

    public:
        OrbitFlyController() = default;

        // ===== ICameraController =====
        void Update(const CameraInputState& input, float deltaTime, Camera& camera) override;
        void ApplyTo(Camera& camera) const override;
        void Reset() override;
        const char* GetDisplayName() const override { return "軌道操作 (Blender風)"; }

        /// @brief プリセットを適用
        void ApplyPreset(Preset preset);

        // ===== 設定 =====
        const Settings& GetSettings() const { return settings_; }
        void SetSettings(const Settings& settings) { settings_ = settings; }

        // ===== 軌道状態 =====
        const OrbitState& GetState() const { return state_; }
        void SetState(const OrbitState& state);

        void SetTarget(const Vector3& target) { state_.target = target; ClampTargetToWorldBounds(); }
        Vector3 GetTarget() const { return state_.target; }

        void SetDistance(float distance);
        float GetDistance() const { return state_.distance; }

        void SetPitch(float pitch) { state_.pitch = pitch; }
        float GetPitch() const { return state_.pitch; }

        void SetYaw(float yaw) { state_.yaw = yaw; }
        float GetYaw() const { return state_.yaw; }

        /// @brief 操作中かどうか
        bool IsControlling() const { return orbiting_ || panning_; }

        /// @brief 角度を -π〜π へ正規化する
        static float NormalizeAngle(float angle);

    private:
        /// @brief 軌道状態から視点ワールド座標を求める
        Vector3 ComputeEyePosition() const;

        /// @brief target を移動範囲へクランプする
        void ClampTargetToWorldBounds();

        /// @brief スムーズ移動用の値を実値へ同期する
        void SyncSmoothToState();

        void HandleOrbitAndPan(const CameraInputState& input);
        void HandleZoom(const CameraInputState& input);
        void HandleFlyMove(const CameraInputState& input, float deltaTime);
        void UpdateSmoothMovement();

    private:
        Settings settings_{};
        OrbitState state_{};

        // スムーズ移動用（表示に使う実効値）
        OrbitState smooth_{};

        // 操作状態（Blender と同じ割り当て：中ドラッグ＝回転 / Shift+中ドラッグ＝パン）
        // ドラッグ開始は押し込みエッジでのみ判定する（押しっぱなしでの誤作動防止）
        bool orbiting_ = false;
        bool panning_ = false;
        bool prevOrbitButton_ = false;

        // ホイールはノッチ単位に達するまで貯める
        int accumulatedWheelDelta_ = 0;
    };
}
