#include "SceneManager.h"
#include "EngineSystem/EngineSystem.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Light/LightManager.h"
#include "Utility/FrameRate/FrameRateController.h"


namespace CoreEngine
{
void SceneManager::Initialize(EngineSystem* engine) {
    engine_ = engine;

    // シーントランジションの初期化
    sceneTransition_ = std::make_unique<SceneTransition>();
    sceneTransition_->Initialize(engine);
}

void SceneManager::SetInitialScene(const std::string& name) {
    // トランジション無しで即座にシーンを読み込む
    DoChangeScene(name);
}

void SceneManager::ChangeScene(std::string name) {
    // デフォルトトランジション（フェード、1秒）
    ChangeScene(std::move(name), SceneTransition::TransitionType::Fade, 1.0f);
}

void SceneManager::ChangeScene(std::string name, SceneTransition::TransitionType transitionType, float duration) {
    // Update/Draw実行中のクラッシュを防ぐため次フレームで切り替え
    nextSceneName_ = std::move(name);
    nextTransitionType_ = transitionType;
    nextTransitionDuration_ = duration;
    isSceneChangeRequested_ = true;
}

void SceneManager::Update() {
    // フレームレート取得
    auto frameRateController = engine_->GetComponent<FrameRateController>();
    float deltaTime = frameRateController ? frameRateController->GetDeltaTime() : 0.016f;

    // トランジション更新
    sceneTransition_->Update(deltaTime);

    // 遅延シーン切り替えのリクエスト処理
    if (isSceneChangeRequested_ && !sceneTransition_->IsTransitioning()) {
        // トランジション開始
        sceneTransition_->StartTransition(nextTransitionType_, nextTransitionDuration_);
        isSceneChangeRequested_ = false;
    }

    // シーン切り替え準備完了時に実際の切り替えを実行
    if (sceneTransition_->IsReadyToChangeScene()) {
        DoChangeScene(nextSceneName_);
        sceneTransition_->OnSceneChanged(); // フェードイン開始
    }

    // トランジションがブロック中でない場合のみシーンを更新
    if (currentScene_ && !sceneTransition_->IsBlocking()) {
        currentScene_->Update();
    }
}

void SceneManager::Draw() {
    if (currentScene_) {
        currentScene_->Draw();
    }
}

void SceneManager::Finalize() {
    // GPUの処理完了を待機してからシーンを解放
    auto dxCommon = engine_->GetComponent<DirectXCommon>();
    if (dxCommon) {
        dxCommon->WaitForPreviousFrame();
    }
    
    currentScene_.reset();
    currentSceneName_ = "None";
    sceneFactories_.clear();

    // トランジションの解放
    sceneTransition_.reset();
}

bool SceneManager::HasScene(const std::string& name) const {
    return sceneFactories_.find(name) != sceneFactories_.end();
}

std::string SceneManager::GetCurrentSceneName() const {
    return currentSceneName_;
}

std::vector<std::string> SceneManager::GetAllSceneNames() const {
    std::vector<std::string> sceneNames;
    sceneNames.reserve(sceneFactories_.size());
    for (const auto& pair : sceneFactories_) {
        sceneNames.push_back(pair.first);
    }
    return sceneNames;
}

bool SceneManager::IsTransitioning() const {
    return sceneTransition_ && sceneTransition_->IsTransitioning();
}

void SceneManager::SkipTransition() {
    if (sceneTransition_) {
        sceneTransition_->SkipTransition();
    }
}

void SceneManager::RegisterSceneBGMCallback(std::function<void(float)> callback) {
    if (sceneTransition_) {
        sceneTransition_->SetBGMVolumeCallback(callback);
    }
}

void SceneManager::DoChangeScene(const std::string& name) {
    auto it = sceneFactories_.find(name);
    if (it == sceneFactories_.end()) {
        return;
    }

    // GPUの処理完了を待機してから古いシーンを解放
    auto dxCommon = engine_->GetComponent<DirectXCommon>();
    if (dxCommon) {
        dxCommon->WaitForPreviousFrame();
    }

    // 古いシーンを解放
    if (currentScene_) {
        // BGMコールバックをクリア
        if (sceneTransition_) {
            sceneTransition_->ClearBGMVolumeCallback();
        }
        currentScene_->Finalize();
    }
    currentScene_.reset();
    
    // シーン切り替え時にライトをクリア
    auto lightManager = engine_->GetComponent<LightManager>();
    if (lightManager) {
        lightManager->ClearAllLights();
    }
    
    // FPS計測をリセット（シーン切り替え時の異常値を防ぐ）
    auto frameRateController = engine_->GetComponent<FrameRateController>();
    if (frameRateController) {
        frameRateController->ResetFPSMeasurement();
    }
    
    // 新しいシーンを作成・初期化
    currentScene_ = it->second();
    currentSceneName_ = name;
    currentScene_->SetSceneManager(this);
    currentScene_->Initialize(engine_);
}
}
