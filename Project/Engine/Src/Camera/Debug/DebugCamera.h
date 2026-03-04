#pragma once
#include "Math/MathCore.h"
#include "Camera/ICamera.h"
#include "Camera/CameraStructs.h"
#include <numbers>
#include <d3d12.h>
#include <wrl.h>

/// @brief デバッグカメラ - 開発時のシーン確認用カメラ

namespace CoreEngine
{

    // 前方宣言
    class EngineSystem;
    class DebugCamera : public ICamera {
    public:
        /// @brief デバッグカメラの操作設定
        struct CameraSettings {
            float rotationSensitivity = 0.003f;     // マウス回転感度（低めに調整）
            float panSensitivity = 0.0005f;    // パン操作感度
            float zoomSensitivity = 1.0f;         // ズーム感度
            float minDistance = 0.1f;    // 最小距離（制限を大幅に緩和）
            float maxDistance = 10000.0f;       // 最大距離（制限を大幅に緩和）
            bool invertY = false;     // Y軸反転
            bool smoothMovement = true;    // スムーズ移動
            float smoothingFactor = 0.2f;           // スムージング係数
        };

        /// @brief カメラプリセット
        enum class CameraPreset {
            Default,        // デフォルト視点
            Front,     // 正面視点
            Back,  // 背面視点
            Left,      // 左側視点
            Right,          // 右側視点
            Top,   // 上から視点
            Bottom,  // 下から視点
            Diagonal,    // 斜め視点
            CloseUp,        // クローズアップ
            Wide      // 広角視点
        };

    public:
        /// @brief コンストラクタ
        DebugCamera();

        /// @brief デストラクタ
        ~DebugCamera() override = default;

        /// @brief 初期化（エンジンシステムの参照を設定）
        /// @param engine エンジンシステムへのポインタ
     /// @param device D3D12デバイス（GPU用リソース作成に必要）
        void Initialize(EngineSystem* engine, ID3D12Device* device);

        /// @brief リセット
        void Reset();

        // ====== ICamera インターフェースの実装 ======

        /// @brief 更新処理（ICamera から）
        void Update() override;

        /// @brief ビュー行列を取得（ICamera から）
        const Matrix4x4& GetViewMatrix() const override { return viewMatrix_; }

        /// @brief プロジェクション行列を取得（ICamera から）
        const Matrix4x4& GetProjectionMatrix() const override { return projectionMatrix_; }

        /// @brief カメラ位置を取得（ICamera から）
        Vector3 GetPosition() const override;

        /// @brief カメラのGPU仮想アドレスを取得（ICamera から）
        D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const override {
            return cameraGPUResource_ ? cameraGPUResource_->GetGPUVirtualAddress() : 0;
        }

        /// @brief 行列をGPUに転送（ICamera から）
        void TransferMatrix() override;

        /// @brief カメラのタイプを取得
        CameraType GetCameraType() const override { return CameraType::Camera3D; }

        /// @brief カメラパラメータを取得
        CameraParameters GetParameters() const override { return parameters_; }

        /// @brief カメラパラメータを設定
        void SetParameters(const CameraParameters& params) override { parameters_ = params; }

        // ====== DebugCamera 固有のアクセッサ ======

        /// @brief カメラ設定を取得
        /// @return カメラ設定の参照
        const CameraSettings& GetSettings() const { return settings_; }

        /// @brief カメラ設定を設定
        /// @param settings 新しいカメラ設定
        void SetSettings(const CameraSettings& settings) { settings_ = settings; }

        /// @brief プリセットを適用
        /// @param preset 適用するプリセット
        void ApplyPreset(CameraPreset preset);

        /// @brief 注視点を設定
        /// @param target 新しい注視点
        void SetTarget(const Vector3& target) { target_ = target; }

        /// @brief 注視点を取得
        /// @return 現在の注視点
        Vector3 GetTarget() const { return target_; }

        /// @brief 距離を設定
        /// @param distance 新しい距離
        void SetDistance(float distance);

        /// @brief 距離を取得
        /// @return 現在の距離
        float GetDistance() const { return distance_; }

        /// @brief ピッチ角を設定（ラジアン）
        /// @param pitch 新しいピッチ角
        void SetPitch(float pitch) { pitch_ = pitch; }

        /// @brief ピッチ角を取得（ラジアン）
        /// @return 現在のピッチ角
        float GetPitch() const { return pitch_; }

        /// @brief ヨー角を設定（ラジアン）
        /// @param yaw 新しいヨー角
        void SetYaw(float yaw) { yaw_ = yaw; }

        /// @brief ヨー角を取得（ラジアン）
        /// @return 現在のヨー角
        float GetYaw() const { return yaw_; }

        /// @brief カメラが操作中かどうか
        /// @return 操作中の場合true
        bool IsControlling() const { return draggingLeft_ || draggingMiddle_; }

    /// @brief カメラパラメータをリセット
    void ResetParameters() { parameters_.Reset(); }

    /// @brief 現在の状態をスナップショットとして保存
    /// @return カメラスナップショット
    CameraSnapshot CaptureSnapshot(const std::string& name = "Snapshot") const;

    /// @brief スナップショットから状態を復元
    /// @param snapshot 復元するスナップショット
    void RestoreSnapshot(const CameraSnapshot& snapshot);

private:
        // カメラパラメータ
        float distance_;       // 注視点からの距離
        float pitch_;                // ピッチ角（ラジアン）
        float yaw_;         // ヨー角（ラジアン）
        Vector3 target_;  // 注視点

        // カメラ行列
        Matrix4x4 viewMatrix_;              // ビュー行列
        Matrix4x4 projectionMatrix_;// プロジェクション行列

        // GPU用リソース（Cameraクラスと同様）
        Microsoft::WRL::ComPtr<ID3D12Resource> cameraGPUResource_;
        CameraForGPU* cameraGPUData_ = nullptr;

        // 操作状態
        bool draggingLeft_;   // 左ドラッグ中
        bool draggingMiddle_;               // 中ドラッグ中

        // 設定
        CameraSettings settings_;       // カメラ設定
        CameraParameters parameters_;   // カメラパラメータ（FOV、クリップ距離など）

        // スムーズ移動用
        Vector3 targetSmooth_;  // スムーズ移動用注視点
        float distanceSmooth_;         // スムーズ移動用距離
        float pitchSmooth_;        // スムーズ移動用ピッチ
        float yawSmooth_;       // スムーズ移動用ヨー

        // エンジンシステム参照
        EngineSystem* engineSystem_ = nullptr;  // エンジンシステムへのポインタ

        // マウス入力処理用
        struct MouseState {
            float lastX = 0.0f;         // 前回のマウスX座標
            float lastY = 0.0f;      // 前回のマウスY座標
            bool leftButtonPressed = false;      // 左ボタンの前回状態
            bool middleButtonPressed = false;    // 中ボタンの前回状態
            int accumulatedWheelDelta = 0;   // 累積ホイール量
        };
        MouseState mouseState_;       // マウス状態管理

    private:
#ifdef _DEBUG
        /// @brief マウス操作を処理（エンジンのMouseInputクラス使用）
        void HandleMouseInput();

        /// @brief シーンウィンドウ内でのマウス操作かを判定
        /// @return シーンウィンドウ内での操作の場合true
        bool IsMouseInSceneWindow() const;
#endif

        /// @brief ビュー行列とプロジェクション行列を更新
        void UpdateMatrices();

        /// @brief プリセット名を取得
        /// @param preset プリセット
      /// @return プリセット名
        const char* GetPresetName(CameraPreset preset) const;

        /// @brief 角度を正規化（-π〜πの範囲に）
        /// @param angle 角度（ラジアン）
        /// @return 正規化された角度
        float NormalizeAngle(float angle) const;

        /// @brief スムーズ移動を更新
        void UpdateSmoothMovement();
    };
}
