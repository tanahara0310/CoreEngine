#include "BaseScene.h"
#include "EngineSystem/EngineSystem.h"
#include "Engine/Camera/CameraManager.h"
#include "Engine/Camera/Debug/DebugCamera.h"
#include "Engine/Camera/Release/Camera.h"
#include "Engine/Camera/Camera2D.h"
#include "Engine/Graphics/Common/DirectXCommon.h"
#include "Engine/Graphics/Light/LightManager.h"
#include "Engine/Graphics/Render/RenderManager.h"
#include "Engine/Graphics/Render/Line/LineRendererPipeline.h"
#include "Engine/Graphics/Line/LineManager.h"
#include "Engine/Graphics/GridRenderer.h"
#include "Engine/Particle/ParticleSystem.h"
#include "Scene/SceneManager.h"
#include "WinApp/WinApp.h"
#include <numbers>

#ifdef _DEBUG
#include <imgui.h>
#include "Engine/Camera/Debug/CameraDebugUI.h"
#endif


namespace CoreEngine
{
    void BaseScene::Initialize(EngineSystem* engine)
    {
        engine_ = engine;

        //カメラ
        SetupCamera();

        //ライト
        SetupLight();

#ifdef _DEBUG
        //グリッド（デバッグビルドのみ）
        SetupGrid();
#endif
    }

    void BaseScene::Update()
    {
        // KeyboardInput を直接取得
        auto keyboard = engine_->GetComponent<KeyboardInput>();
        if (!keyboard) {
            return; // キーボードは必須
        }
#ifdef _DEBUG

        // デバッグカメラへの切り替え
        if (keyboard->IsKeyTriggered(DIK_F1)) {
            cameraManager_->SetActiveCamera("Debug", CameraType::Camera3D);
        } else if (keyboard->IsKeyTriggered(DIK_F2)) {
            cameraManager_->SetActiveCamera("Release", CameraType::Camera3D);

        }

#endif // _DEBUG


        // カメラの更新
        if (cameraManager_) {
            cameraManager_->Update();
        }

        // ライトマネージャーの更新
        auto lightManager = engine_->GetComponent<LightManager>();
        if (lightManager) {
            lightManager->UpdateAll();
        }

#ifdef _DEBUG
        // カメラマネージャーのImGui
        if (cameraManager_) {
            cameraManager_->DrawImGui();
        }

        // LineManagerのImGui
        LineManager::GetInstance().DrawImGui();

        // ゲームオブジェクトのImGuiデバッグUI表示
        if (ImGui::Begin("オブジェクト制御")) {
            gameObjectManager_.DrawAllImGui();

        }
        ImGui::End();
#endif

        // 派生クラスの更新処理（GameObjectの更新前）
        OnUpdate();

#ifdef _DEBUG
        // LineManagerのデバッグ描画（OnUpdate後に実行）
        LineManager::GetInstance().DrawDebugShapes();
#endif

        // ゲームオブジェクトの更新
        gameObjectManager_.UpdateAll();

        // 派生クラスの後処理（クリーンアップ前）
        OnLateUpdate();

        // フレーム終了時に削除マークされたオブジェクトをクリーンアップ
        // すべての更新処理が完了した後に実行することで、
        // 派生クラスが保持する生ポインタが無効化される前に安全に処理できる
        gameObjectManager_.CleanupDestroyed();
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
    }

    void BaseScene::Finalize()
    {
        // ゲームオブジェクトをクリア（新システム）
        gameObjectManager_.Clear();
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
                directionalLight_->direction = MathCore::Vector::Normalize({ 0.0f, -1.0f, 0.5f });
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
    }
#endif

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
}
