#pragma once

#include "ISceneFeature.h"
#include "Math/Vector/Vector3.h"
#include <memory>
#include <string>
#include <vector>

namespace CoreEngine
{
    class Camera;
    class CameraManager;
    class OrbitFlyController;

    /// @brief シーンのカメラ一式（ゲーム視点・エディタ視点・2D）を所有する Feature
    /// @details 生成・毎フレームの操作反映・エディタ視点の控えまでをここに閉じる。
    ///          Scene は所有せず、SceneContext と GetGameViewCamera3D() へ
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
        /// @details シーンのオブジェクトが出そろった後に走る。ここで復元すると、
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

        /// 既定 GameView カメラの高さ。
        /// 大気散乱は「カメラ高度 - groundLevelY」を惑星中心距離へ変換するため、
        /// 地表 y=0 と同じ高さに置くと地平線が特異点に近づく。必ず y > 0 に保つこと。
        static constexpr float kDefaultCameraHeight = 3.0f;

    private:
        /// @brief エディタ視点カメラの設定・姿勢を控える
        void CaptureEditorCamera();

        /// @brief シーンに置かれたカメラ（`CameraComponent`）を実体へ写す
        /// @details オブジェクトが増減・改名したら実体を作り直し、姿勢とレンズを毎フレーム流す。
        ///          ゲームの視点にするカメラの名前もここで決めて控える。
        void SyncSceneCameras(SceneContext& ctx);

        /// @brief 控えた「ゲームの視点」をカメラマネージャーへ当てる
        /// @details シーンに候補が無いときは、エンジン既定のカメラへ戻す。
        void ApplyMainCamera();

        std::unique_ptr<CameraManager> cameraManager_;

        // 設定・姿勢を毎フレーム控えるエディタ視点カメラ（所有は cameraManager_）
        Camera* sceneCamera_ = nullptr;
        OrbitFlyController* orbitController_ = nullptr;

        // 実体を持っているシーンのカメラの名前（増減を見分けるための控え）
        std::vector<std::string> sceneCameraNames_;

        // ゲームの視点にするシーンのカメラの名前（候補が無ければ空）
        std::string mainCameraName_;
    };
}
