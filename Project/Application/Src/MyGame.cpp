#include "pch.h"
#include "MyGame.h"
#include <EngineSystem/EngineSystem.h>
#include "WinApp/WinApp.h"
#include "Scenes/TestScene/TestScene.h"
#include "Scenes/ParticleTestScene/ParticleTestScene.h"
#include "Scenes/PrimitiveTestScene/PrimitiveTestScene.h"
#include "Scenes/SpriteTestScene/SpriteTestScene.h"
#include "Scenes/WaterTestScene/WaterTestScene.h"
#include "Scenes/AtmosphereTestScene/AtmosphereTestScene.h"
#include "Scenes/VolumetricCloudTestScene/VolumetricCloudTestScene.h"
#include "Scenes/HomeworkScene/HomeworkScene.h"

using namespace CoreEngine;

MyGame::~MyGame() = default;

void MyGame::Initialize()
{
    // ──────────────────────────────────────────────────────────
    // シーン管理システムの初期化
    // ──────────────────────────────────────────────────────────

    sceneManager_ = std::make_unique<CoreEngine::SceneManager>();
    sceneManager_->Initialize(GetEngineSystem());
    GetEngineSystem()->SetSceneManager(sceneManager_.get());

    // 全シーンを登録（アプリ層で実装）
    sceneManager_->RegisterScene<TestScene>("TestScene");
    sceneManager_->RegisterScene<ParticleTestScene>("ParticleTestScene");
    sceneManager_->RegisterScene<PrimitiveTestScene>("PrimitiveTestScene");
    sceneManager_->RegisterScene<SpriteTestScene>("SpriteTestScene");
    sceneManager_->RegisterScene<WaterTestScene>("WaterTestScene");
    sceneManager_->RegisterScene<AtmosphereTestScene>("AtmosphereTestScene");
    sceneManager_->RegisterScene<VolumetricCloudTestScene>("VolumetricCloudTestScene");
    sceneManager_->RegisterScene<HomeworkScene>("HomeworkScene");

    // 初期シーンを設定（トランジション無し）
    sceneManager_->SetInitialScene("ParticleTestScene"); // TODO: GPUパーティクル検証後に PrimitiveTestScene へ戻す

    // ===== コンソールログ出力とシーンマネージャーの設定 =====
#ifdef USE_IMGUI
    // GameDebugUIにSceneManagerを設定
    auto gameDebugUI = GetEngineSystem()->GetDebugSubsystem()->GetGameDebugUI();
    if (gameDebugUI) {
        gameDebugUI->SetSceneManager(sceneManager_.get());
    }
#endif

#ifdef USE_IMGUI
    auto console = GetEngineSystem()->GetDebugSubsystem()->GetConsole();
    if (console) {
        console->LogInfo("MyGame: ゲーム初期化が完了しました");
        console->LogInfo("MyGame: 初期シーン 'AssignmentScene' を読み込みました");
    }
#endif
}

void MyGame::Finalize()
{
    // ──────────────────────────────────────────────────────────
    // シーン管理システムの終了処理
    // ──────────────────────────────────────────────────────────

#ifdef USE_IMGUI
    auto console = GetEngineSystem()->GetDebugSubsystem()->GetConsole();
    if (console) {
        console->LogInfo("MyGame: ゲーム終了処理を開始しました");
    }
#endif

    if (sceneManager_) {
        GetEngineSystem()->SetSceneManager(nullptr);
        sceneManager_->Finalize();
        sceneManager_.reset();
    }
}

void MyGame::Update()
{
    // ──────────────────────────────────────────────────────────
    // デバッグUIからのシーン切り替えリクエストを処理
    // ──────────────────────────────────────────────────────────
#ifdef USE_IMGUI
    auto gameDebugUI = GetEngineSystem()->GetDebugSubsystem()->GetGameDebugUI();
    if (gameDebugUI) {
        auto sceneManagerTab = gameDebugUI->GetSceneManagerTab();
        if (sceneManagerTab && sceneManagerTab->IsChangeRequested()) {
            std::string requestedScene = sceneManagerTab->GetRequestedSceneName();
            if (sceneManager_ && sceneManager_->HasScene(requestedScene)) {
                sceneManager_->ChangeScene(requestedScene);

#ifdef USE_IMGUI
                auto console = GetEngineSystem()->GetDebugSubsystem()->GetConsole();
                if (console) {
                    console->LogInfo("シーン切り替え: " + requestedScene);
                }
#endif
            }
            sceneManagerTab->ResetChangeRequest();
        }
    }
#endif

    // ──────────────────────────────────────────────────────────
    // シーン更新処理を委譲
    // ──────────────────────────────────────────────────────────

    if (sceneManager_) {
        sceneManager_->Update();
    }
}

void MyGame::Draw()
{
    // ──────────────────────────────────────────────────────────
    // シーン描画処理を委譲
    // ──────────────────────────────────────────────────────────

    if (sceneManager_) {
        sceneManager_->Draw();
    }
}

void MyGame::PrepareRender()
{
    if (sceneManager_) {
        sceneManager_->PrepareRender();
    }
}
