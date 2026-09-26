#include "pch.h"
#include "MyGame.h"
#include <EngineSystem/EngineSystem.h>
#include <EngineSystem/Startup/StartupSequence.h>
#include "WinApp/WinApp.h"
#include "Scene/SceneSaveSystem.h"
#include "EngineSystem/Settings/ProjectSettings.h"
#include "Graphics/Model/ModelManager.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace CoreEngine;

MyGame::~MyGame() = default;

void MyGame::Initialize()
{
    CreateSceneManager();
    LoadInitialScene();
    ConnectDebugUI();
}

std::string MyGame::ResolveInitialSceneName()
{
    const std::vector<std::string> scenes = CoreEngine::SceneSaveSystem::ListSavedScenes();
    const std::string& wanted = CoreEngine::ProjectSettings::Get().GetInitialSceneName();

    if (!wanted.empty()
        && std::find(scenes.begin(), scenes.end(), wanted) != scenes.end()) {
        return wanted;
    }
    // 設定が空か、指していたシーンが消えている。開けるものを出しておく
    return scenes.empty() ? std::string{} : scenes.front();
}

void MyGame::BuildStartupTasks(CoreEngine::StartupSequence& sequence)
{
    sequence.Add("シーン管理システム", [this] { CreateSceneManager(); });
    sequence.Add("シーン構築: " + ResolveInitialSceneName(), [this] { LoadInitialScene(); });
    sequence.Add("デバッグUI 接続", [this] { ConnectDebugUI(); });
}

void MyGame::BuildPreloadTasks(CoreEngine::StartupSequence& sequence)
{
    const std::string sceneName = ResolveInitialSceneName();
    sequence.Add("モデル先読み開始: " + sceneName, [this, sceneName] {
        // シーン JSON から modelPath だけを抜き出す（オブジェクトはまだ作らない）
        const std::vector<std::string> modelPaths =
            CoreEngine::SceneSaveSystem::CollectModelPaths(sceneName);

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
            "モデル先読みを開始: {} 件（シーン: {}）", modelPaths.size(), sceneName);
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

}

void MyGame::LoadInitialScene()
{
    const std::string sceneName = ResolveInitialSceneName();
    if (sceneName.empty()) {
        CoreEngine::Logger::GetInstance().Logf(
            CoreEngine::LogLevel::Error, CoreEngine::LogCategory::System,
            "開けるシーンがありません（Application/Assets/Scenes が空か、"
            "プロジェクト設定の initialScene が指すシーンが見つかりません）");
        return;
    }
    // 初期シーンを設定（トランジション無し）
    sceneManager_->SetInitialScene(sceneName);
}

void MyGame::ConnectDebugUI()
{
    // ===== コンソールログ出力とシーンマネージャーの設定 =====
#ifdef CORE_EDITOR
    // GameDebugUIにSceneManagerを設定
    auto gameDebugUI = GetEngineSystem()->GetDebugSubsystem()->GetGameDebugUI();
    if (gameDebugUI) {
        gameDebugUI->SetSceneManager(sceneManager_.get());
    }

    auto console = GetEngineSystem()->GetDebugSubsystem()->GetConsole();
    if (console) {
        console->LogInfo("MyGame: ゲーム初期化が完了しました");
        console->LogInfo("MyGame: 初期シーン '" + ResolveInitialSceneName() + "' を読み込みました");
    }
#endif
}

void MyGame::Finalize()
{
    // ──────────────────────────────────────────────────────────
    // シーン管理システムの終了処理
    // ──────────────────────────────────────────────────────────

#ifdef CORE_EDITOR
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
    // シーン更新処理を委譲
    // ──────────────────────────────────────────────────────────

    if (sceneManager_) {
        sceneManager_->Update();
    }
}

void MyGame::PrepareRender()
{
    if (sceneManager_) {
        sceneManager_->PrepareRender();
    }
}
