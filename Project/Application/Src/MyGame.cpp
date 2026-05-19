#include "MyGame.h"
#include <EngineSystem/EngineSystem.h>
#include "WinApp/WinApp.h"
#include "Sample/SampleScene/TestScene/TestScene.h"
#include "Sample/SampleScene/ParticleTestScene/ParticleTestScene.h"
#include "Sample/SampleScene/PrimitiveTestScene/PrimitiveTestScene.h"
#include "Sample/SampleScene/SpriteTestScene/SpriteTestScene.h"
#include "Sample/SampleScene/WaterTestScene/WaterTestScene.h"
#include "Sample/SampleScene/HomeworkScene/HomeworkScene.h"

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
    sceneManager_->RegisterScene<HomeworkScene>("HomeworkScene");

    // 初期シーンを設定（トランジション無し）
    sceneManager_->SetInitialScene("WaterTestScene");

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
