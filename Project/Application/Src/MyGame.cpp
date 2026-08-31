#include "pch.h"
#include "MyGame.h"
#include <EngineSystem/EngineSystem.h>
#include <EngineSystem/Startup/StartupSequence.h>
#include "WinApp/WinApp.h"
#include "Scene/SceneSaveSystem.h"
#include "Graphics/Model/ModelManager.h"
#include "Utility/Logger/Logger.h"
#include "Scenes/TestScene/TestScene.h"
#include "Scenes/WaterTestScene/WaterTestScene.h"
#include "Scenes/CollisionTestScene/CollisionTestScene.h"
#include "Scenes/SampleGameScene/SampleGameScene.h"
#include "Scenes/ShootingSampleScene/ShootingSampleScene.h"
#include "Scenes/Sprite2DSampleScene/Sprite2DSampleScene.h"
#include "Scenes/MsdfTextTestScene/MsdfTextTestScene.h"

using namespace CoreEngine;

MyGame::~MyGame() = default;

void MyGame::Initialize()
{
    CreateSceneManager();
    LoadInitialScene();
    ConnectDebugUI();
}

void MyGame::BuildStartupTasks(CoreEngine::StartupSequence& sequence)
{
    sequence.Add("シーン管理システム", [this] { CreateSceneManager(); });
    sequence.Add(std::string("シーン構築: ") + kInitialSceneName, [this] { LoadInitialScene(); });
    sequence.Add("デバッグUI 接続", [this] { ConnectDebugUI(); });
}

void MyGame::BuildPreloadTasks(CoreEngine::StartupSequence& sequence)
{
    sequence.Add(std::string("モデル先読み開始: ") + kInitialSceneName, [this] {
        // シーン JSON から modelPath だけを抜き出す（オブジェクトはまだ作らない）
        const std::vector<std::string> modelPaths =
            CoreEngine::SceneSaveSystem::CollectModelPaths(kInitialSceneName);

        if (modelPaths.empty()) {
            return;
        }

        auto* modelManager = GetEngineSystem()->GetService<CoreEngine::ModelManager>();
        if (!modelManager) {
            return;
        }

        // ここは投げるだけで即座に戻る。実際のロードは以降のシェーダコンパイル中に
        // ワーカーで進み、シーン構築時の CreateStaticModel が
        // ModelManager のロード権待ちで合流する
        modelManager->BeginPreload(modelPaths);

        CoreEngine::Logger::GetInstance().Logf(
            CoreEngine::LogLevel::Info, CoreEngine::LogCategory::Resource,
            "モデル先読みを開始: {} 件（シーン: {}）", modelPaths.size(), kInitialSceneName);
    });
}

void MyGame::CreateSceneManager()
{
    // ──────────────────────────────────────────────────────────
    // シーン管理システムの初期化
    // ──────────────────────────────────────────────────────────

    sceneManager_ = std::make_unique<CoreEngine::SceneManager>();
    sceneManager_->Initialize(GetEngineSystem());
    GetEngineSystem()->SetSceneManager(sceneManager_.get());

    // 全シーンを登録（アプリ層で実装）
    sceneManager_->RegisterScene<TestScene>("TestScene");
    sceneManager_->RegisterScene<WaterTestScene>("WaterTestScene");
    // 当たり判定の回帰テストシーン（Scene Manager タブから切り替えて使う）
    sceneManager_->RegisterScene<CollisionTest::CollisionTestScene>("CollisionTestScene");
    // 学習用のサンプルゲーム
    sceneManager_->RegisterScene<SampleGame::SampleGameScene>("SampleGameScene");
    sceneManager_->RegisterScene<ShootingSample::ShootingSampleScene>("ShootingSampleScene");
    sceneManager_->RegisterScene<Sprite2DSample::Sprite2DSampleScene>("Sprite2DSampleScene");
    // MSDF フォント描画の検証シーン
    sceneManager_->RegisterScene<MsdfTextTest::MsdfTextTestScene>("MsdfTextTestScene");
}

void MyGame::LoadInitialScene()
{
    // 初期シーンを設定（トランジション無し）
    sceneManager_->SetInitialScene(kInitialSceneName);
}

void MyGame::ConnectDebugUI()
{
    // ===== コンソールログ出力とシーンマネージャーの設定 =====
#ifdef USE_IMGUI
    // GameDebugUIにSceneManagerを設定
    auto gameDebugUI = GetEngineSystem()->GetDebugSubsystem()->GetGameDebugUI();
    if (gameDebugUI) {
        gameDebugUI->SetSceneManager(sceneManager_.get());
    }

    auto console = GetEngineSystem()->GetDebugSubsystem()->GetConsole();
    if (console) {
        console->LogInfo("MyGame: ゲーム初期化が完了しました");
        console->LogInfo(std::string("MyGame: 初期シーン '") + kInitialSceneName + "' を読み込みました");
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
