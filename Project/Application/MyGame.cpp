#include "MyGame.h"
#include <EngineSystem.h>
#include "WinApp/WinApp.h"
#include "Scene/TestScene/TestScene.h"
#include "Scene/ParticleTestScene/ParticleTestScene.h"

using namespace CoreEngine;

MyGame::~MyGame() = default;

void MyGame::Initialize()
{
    // ──────────────────────────────────────────────────────────
    // シーン管理システムの初期化
    // ──────────────────────────────────────────────────────────

    sceneManager_ = std::make_unique<CoreEngine::SceneManager>();
    sceneManager_->Initialize(GetEngineSystem());

    // 全シーンを登録（アプリ層で実装）
    sceneManager_->RegisterScene<TestScene>("TestScene");
    sceneManager_->RegisterScene<ParticleTestScene>("ParticleTestScene");

    // 初期シーンを設定（トランジション無し）
    sceneManager_->SetInitialScene("TestScene");

    // ===== コンソールログ出力とシーンマネージャーの設定 =====
#ifdef _DEBUG
    auto console = GetEngineSystem()->GetConsole();
    if (console) {
        console->LogInfo("MyGame: ゲーム初期化が完了しました");
        console->LogInfo("MyGame: 初期シーン 'AssignmentScene' を読み込みました");
    }

    // GameDebugUIにSceneManagerを設定
    auto gameDebugUI = GetEngineSystem()->GetGameDebugUI();
    if (gameDebugUI) {
        gameDebugUI->SetSceneManager(sceneManager_.get());
    }
#endif
}

void MyGame::Finalize()
{
    // ──────────────────────────────────────────────────────────
    // シーン管理システムの終了処理
    // ──────────────────────────────────────────────────────────

#ifdef _DEBUG
    auto console = GetEngineSystem()->GetConsole();
    if (console) {
        console->LogInfo("MyGame: ゲーム終了処理を開始しました");
    }
#endif

    if (sceneManager_) {
        sceneManager_->Finalize();
        sceneManager_.reset();
    }
}

void MyGame::Update()
{
    // ──────────────────────────────────────────────────────────
    // デバッグUIからのシーン切り替えリクエストを処理
    // ──────────────────────────────────────────────────────────
#ifdef _DEBUG
    auto gameDebugUI = GetEngineSystem()->GetGameDebugUI();
    if (gameDebugUI) {
        auto sceneManagerTab = gameDebugUI->GetSceneManagerTab();
        if (sceneManagerTab && sceneManagerTab->IsChangeRequested()) {
            std::string requestedScene = sceneManagerTab->GetRequestedSceneName();
            if (sceneManager_ && sceneManager_->HasScene(requestedScene)) {
                sceneManager_->ChangeScene(requestedScene);

                auto console = GetEngineSystem()->GetConsole();
                if (console) {
                    console->LogInfo("シーン切り替え: " + requestedScene);
                }
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
