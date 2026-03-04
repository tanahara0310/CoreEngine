#include "BaseScene.h"
#include "EngineSystem/EngineSystem.h"
#include "Camera/CameraManager.h"
#include "Camera/Debug/DebugCamera.h"
#include "Camera/Release/Camera.h"
#include "Camera/Camera2D.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/Line/LineRendererPipeline.h"
#include "Graphics/Line/LineManager.h"
#include "Graphics/Render/Line/GridRenderer.h"
#include "Particle/ParticleSystem.h"
#include "Scene/SceneManager.h"
#include "ObjectCommon/SpriteObject.h"
#include "WinApp/WinApp.h"
#include <numbers>


namespace CoreEngine
{
    void BaseScene::Initialize(EngineSystem* engine)
    {
        engine_ = engine;

        // シーン保存システム
        sceneSaveSystem_ = std::make_unique<SceneSaveSystem>();

        //カメラ
        SetupCamera();

        //ライト
        SetupLight();

#ifdef _DEBUG
        //グリッド（デバッグビルドのみ）
        SetupGrid();

        // デバッグエディター初期化
        debugEditor_ = std::make_unique<SceneDebugEditor>();
        debugEditor_->Initialize(engine_, &gameObjectManager_, cameraManager_.get(), sceneSaveSystem_.get());
#endif
    }

    void BaseScene::Update()
    {
        // カメラの更新
        if (cameraManager_) {
            cameraManager_->Update();
        }

        // ライトマネージャーの更新
        auto lightManager = engine_->GetComponent<LightManager>();
        if (lightManager) {
            lightManager->UpdateAll();

            // シャドウマップ用のライトView-Projection行列を更新
            UpdateLightViewProjection();
        }

#ifdef _DEBUG
        debugEditor_->Update();
#endif

        // 派生クラスの更新処理（GameObjectの更新前）
        OnUpdate();

        // ゲームオブジェクトの更新
        gameObjectManager_.UpdateAll();

        // コリジョン判定（毎フレーム: 収集 → 判定）
        // ClearColliders()でコライダーリストのみリセット（previousCollisions_は保持してEnter/Stay/Exitを正しく検出）
        collisionManager_.ClearColliders();
        gameObjectManager_.RegisterAllColliders(&collisionManager_);
        collisionManager_.CheckAllCollisions();

        // 派生クラスの後処理（クリーンアップ前）
        OnLateUpdate();
    }

    void BaseScene::Draw()
    {
        auto renderManager = engine_->GetComponent<RenderManager>();
        auto dxCommon = engine_->GetComponent<DirectXCommon>();
        ICamera* activeCamera3D = cameraManager_->GetActiveCamera(CameraType::Camera3D);

        if (!renderManager || !dxCommon || !activeCamera3D) {
            return;
        }

        ID3D12GraphicsCommandList* cmdList = dxCommon->GetCommandList();

        // ===== RenderManagerによる統一描画システム =====
        // カメラマネージャーを設定（タイプ別カメラを自動選択）
        renderManager->SetCameraManager(cameraManager_.get());
        renderManager->SetCommandList(cmdList);

        // 全てのゲームオブジェクトを描画キューに追加
        gameObjectManager_.RegisterAllToRender(renderManager);

        // 一括描画（自動的にパスごとにソート・グループ化）
        // この中でGridRenderer、LineDrawable、ParticleSystemのデバッグラインが全て描画される
        renderManager->DrawAll();

        // フレーム終了時にキューをクリア
        renderManager->ClearQueue();

        // 描画完了後に削除マークされたオブジェクトをクリーンアップ
        gameObjectManager_.CleanupDestroyed();
    }

    void BaseScene::Finalize()
    {
        // ゲームオブジェクトをクリア（新システム）
        gameObjectManager_.Clear();

#ifdef _DEBUG
        // デバッグ編集履歴をクリア
        debugEditor_->ClearHistory();
#endif
    }

    void BaseScene::SetupCamera()
    {
        auto dxCommon = engine_->GetComponent<DirectXCommon>();
        if (!dxCommon) {
            return;
        }

        // カメラマネージャーを作成
        cameraManager_ = std::make_unique<CameraManager>();

        // ===== 3Dカメラの設定 =====

        // リリースカメラを作成して登録（斜め上から俯瞰する視点）
        auto releaseCamera = std::make_unique<Camera>();
        releaseCamera->Initialize(dxCommon->GetDevice());
        releaseCamera->SetTranslate({ 0.0f, 24.0f, -24.0f });
        releaseCamera->SetRotate({ 0.8f, 0.0f, 0.0f });

        cameraManager_->RegisterCamera("Release", std::move(releaseCamera));

        // デバッグカメラを作成して登録
        auto debugCamera = std::make_unique<DebugCamera>();
        debugCamera->Initialize(engine_, dxCommon->GetDevice());
        cameraManager_->RegisterCamera("Debug", std::move(debugCamera));

        // デフォルトでリリースカメラをアクティブに設定
        cameraManager_->SetActiveCamera("Release", CameraType::Camera3D);

        // ===== 2Dカメラの設定 =====

        // 2Dカメラを作成して登録（スクリーンサイズは自動取得）
        auto camera2D = std::make_unique<Camera2D>();
        // 2Dカメラの初期位置：画面中央
        camera2D->SetPosition(Vector2{ 0.0f, 0.0f });
        camera2D->SetZoom(1.0f);

        cameraManager_->RegisterCamera("Camera2D", std::move(camera2D));

        // 2Dカメラをアクティブに設定
        cameraManager_->SetActiveCamera("Camera2D", CameraType::Camera2D);
    }

    void BaseScene::SetupLight()
    {
        // デフォルトのディレクショナルライトを設定
        auto lightManager = engine_->GetComponent<LightManager>();
        if (lightManager) {
            directionalLight_ = lightManager->AddDirectionalLight();
            if (directionalLight_) {
                directionalLight_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
                directionalLight_->direction = MathCore::Vector::Normalize({ 0.0f, -1.0f, 0.0f });
                directionalLight_->intensity = 1.0f;
                directionalLight_->enabled = true;
            }
        }
    }

#ifdef _DEBUG
    void BaseScene::SetupGrid()
    {
        // GridRendererを作成
        gridRenderer_ = CreateObject<GridRenderer>();
        gridRenderer_->Initialize();

        // デフォルト設定
        gridRenderer_->SetGridSize(100.0f);
        gridRenderer_->SetSpacing(1.0f);
        gridRenderer_->SetVisible(true);

        // グリッドはシーンデータに保存しない
        gridRenderer_->SetSerializeEnabled(false);
    }
#endif

    void BaseScene::UpdateLightViewProjection()
    {
        // ディレクショナルライトが有効な場合のみ、シャドウマップ用の行列を計算
        if (!directionalLight_ || !directionalLight_->enabled) {
            return;
        }

        // ライト位置を計算（ライト方向の逆方向、シーン中心から一定距離）
        Vector3 lightDir = MathCore::Vector::Normalize(directionalLight_->direction);
        Vector3 lightPos = MathCore::Vector::Multiply(-kShadowLightDistance, lightDir);

        // ライトのビュー行列（シーン中心を見る）
        Vector3 target = { 0.0f, 0.0f, 0.0f };
        Vector3 up = { 0.0f, 1.0f, 0.0f };
        Matrix4x4 lightView = MathCore::Matrix::LookAt(lightPos, target, up);

        // シャドウマップ用の正射影行列
        Matrix4x4 lightProjection = MathCore::Rendering::Orthographic(
            -kShadowOrthoSize, kShadowOrthoSize,  // 左、上
            kShadowOrthoSize, -kShadowOrthoSize,  // 右、下
            kShadowNearPlane, kShadowFarPlane      // 近平面、遠平面
        );

        // View * Projection
        Matrix4x4 lightViewProjection = MathCore::Matrix::Multiply(lightView, lightProjection);

        // RenderManagerに設定
        auto renderManager = engine_->GetComponent<RenderManager>();
        if (renderManager) {
            renderManager->SetLightViewProjection(lightViewProjection);
        }
    }

    void BaseScene::RegisterSceneBGM(std::unique_ptr<SoundManager::SoundResource>* bgm) {
        sceneBGM_ = bgm;

        // 現在設定されているBGM音量を取得
        if (bgm && *bgm && (*bgm)->IsValid()) {
            baseBGMVolume_ = (*bgm)->GetVolume();
        }

        // SceneManager経由でBGMコールバックを登録
        if (sceneManager_) {
            sceneManager_->RegisterSceneBGMCallback([this](float volumeMultiplier) {
                if (sceneBGM_ && *sceneBGM_ && (*sceneBGM_)->IsValid()) {
                    // 基本音量 × トランジション倍率で音量を設定
                    (*sceneBGM_)->SetVolume(baseBGMVolume_ * volumeMultiplier);
                }
                });
        }
    }

    void BaseScene::LoadObjectsFromJson()
    {
        sceneSaveSystem_->Load(&gameObjectManager_);
    }

    void BaseScene::SaveObjectsToJson()
    {
        sceneSaveSystem_->Save(&gameObjectManager_);
    }

    void BaseScene::SaveSingleObjectToJson(GameObject* obj)
    {
        sceneSaveSystem_->SaveSingle(obj);
    }
}

