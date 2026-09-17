#pragma once

#include "ISceneFeature.h"
#include "Math/Vector/Vector3.h"
#include <memory>

namespace CoreEngine
{
    class Camera;
    class CameraManager;
    class OrbitFlyController;

    /// @brief シーンのカメラ一式（ゲーム視点・エディタ視点・2D）を所有する Feature
    /// @details 生成・毎フレームの操作反映・エディタ視点の控えまでをここに閉じる。
    ///          BaseScene は所有せず、SceneContext と GetGameViewCamera3D() へ
    ///          渡すための非所有ポインタだけを持つ。
    /// @note FrameStart の最初（kEarlyFeaturePriority）で回すこと。
    ///       他の Feature（ライト/影・床の追従・大気散乱）はいずれも
    ///       「今フレームのカメラ姿勢が確定済み」であることを前提にしている。
    class CameraFeature : public ISceneFeature {
    public:
        /// @brief 既定コンストラクタ／デストラクタ
        /// @note CameraManager を前方宣言のままにするため、どちらも .cpp で定義する
        ///       （unique_ptr メンバの破棄には完全型が要る。コンストラクタも
        ///        例外巻き戻しのために同じものを必要とする）。
        CameraFeature();
        ~CameraFeature() override;

        const char* GetName() const override { return "Camera"; }

        /// @brief カメラ一式を生成し、エディタ視点へ控えの設定・姿勢を当てる
        /// @note GraphicsCore が未登録の場合は何も生成しない（GetCameraManager() は nullptr）。
        void Initialize(SceneContext& ctx) override;

        /// @brief シーンに保存されたカメラ状態を復元する
        /// @details シーン側の OnInitialize が構図を決めた後に走る。ここで復元すると、
        ///          エディタで詰めた構図がシーンのコードより優先される。
        void PostSceneInitialize(SceneContext& ctx) override;

        /// @brief FrameStart でカメラ操作を反映し、エディタ視点の設定・姿勢を控える
        void Update(SceneContext& ctx, SceneUpdatePhase phase) override;

        /// @brief 停止中も回す（止めるとエディタカメラを動かせなくなる）
        bool RunsWhileStopped() const override { return true; }

        /// @brief 最後の設定・姿勢を控える（カメラの破棄より先に行う）
        void Finalize(SceneContext& ctx) override;

        /// @brief カメラマネージャーを取得（未生成なら nullptr）
        CameraManager* GetCameraManager() const { return cameraManager_.get(); }

        /// @brief ゲーム視点カメラ（CameraNames::Game）の位置・回転を上書きする
        void SetReleaseCameraTransform(const Vector3& translate, const Vector3& rotate);

        /// @brief ゲーム視点カメラ（CameraNames::Game）のレンズを上書きする
        void SetReleaseCameraLens(float fovDegrees, float farClip, float nearClip);

        /// 既定 GameView カメラの高さ。
        /// 大気散乱は「カメラ高度 - groundLevelY」を惑星中心距離へ変換するため、
        /// 地表 y=0 と同じ高さに置くと地平線が特異点に近づく。必ず y > 0 に保つこと。
        static constexpr float kDefaultCameraHeight = 3.0f;

    private:
        /// @brief エディタ視点カメラの設定・姿勢を控える
        void CaptureEditorCamera();

        std::unique_ptr<CameraManager> cameraManager_;

        // 設定・姿勢を毎フレーム控えるエディタ視点カメラ（所有は cameraManager_）
        Camera* sceneCamera_ = nullptr;
        OrbitFlyController* orbitController_ = nullptr;
    };
}
