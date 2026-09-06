#include "pch.h"
#include "SceneManager.h"
#include "EngineSystem/EngineSystem.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/Pass/RenderPipeline.h"
#include "Utility/FrameRate/FrameRateController.h"
#include "Utility/FrameRate/Time.h"
#include "GameObject/GameObjectManager.h"
#include "EngineSystem/Startup/StartupSequence.h"
#include "Utility/Logger/Logger.h"
#include <thread>


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
        // デフォルトトランジション（ローディング画面）
        ChangeScene(std::move(name), SceneTransition::TransitionType::Loading, 0.4f);
    }

    void SceneManager::ChangeScene(std::string name, SceneTransition::TransitionType transitionType, float duration) {
        // Update/Draw実行中のクラッシュを防ぐため次フレームで切り替え
        nextSceneName_ = std::move(name);
        nextTransitionType_ = transitionType;
        nextTransitionDuration_ = duration;
        isSceneChangeRequested_ = true;
    }

    void SceneManager::Update() {
        // トランジション更新（ポーズやスローの影響を受けない）
        sceneTransition_->Update(Time::UnscaledDeltaTime());

        // 遅延シーン切り替えのリクエスト処理
        if (isSceneChangeRequested_ && !sceneTransition_->IsTransitioning()) {
            // トランジション開始
            sceneTransition_->StartTransition(nextTransitionType_, nextTransitionDuration_);
            isSceneChangeRequested_ = false;
        }

        // シーン切り替え準備完了後は 1 フレームに 1 ステップずつ構築する
        if (sceneTransition_->IsReadyToChangeScene()) {
            if (!IsSceneLoadInProgress() && !BeginSceneLoad(nextSceneName_)) {
                sceneTransition_->OnSceneChanged(); // 未登録のシーン名
            }
            if (IsSceneLoadInProgress()) {
                StepSceneLoad();
                sceneTransition_->SetLoadProgress(GetSceneLoadProgress());
                if (!IsSceneLoadInProgress()) {
                    sceneTransition_->OnSceneChanged(); // フェードイン開始
                }
            }
        }

        // トランジションのブロック中は「ゲームの進行だけ」を止める。
        // シーンごと更新を飛ばしてはいけない。大気・雲・フォグは毎フレームの Update で
        // 「このフレーム有効」フラグを立て直しており（RenderDomainContext::EndFrame が
        // 毎フレーム落とす）、飛ばすとフェードアウトの 1 フレーム目 ―― まだ絵が
        // ほぼ素通しで見えている時点 ―― で雲・雲影・フォグが消え、
        // 露出も照明駆動測光を失って画面が明るく浮く
        if (currentScene_) {
            currentScene_->Update(sceneTransition_->IsBlocking()
                ? SceneUpdateMode::Suspended
                : SceneUpdateMode::Full);
        }
    }

    void SceneManager::PrepareRender() {
        if (currentScene_) {
            currentScene_->PrepareRender();
        }
    }

    void SceneManager::FinalizeRenderFrame() {
        if (!currentScene_) {
            return;
        }

        if (auto* renderManager = engine_->GetService<RenderManager>()) {
            renderManager->ClearQueue();
        }

        if (auto* objMgr = currentScene_->GetGameObjectManager()) {
            objMgr->CleanupDestroyed();
        }
    }

    void SceneManager::Finalize() {
        // GPUの処理完了を待機してからシーンを解放
        auto dxCommon = engine_->GetService<GraphicsCore>();
        if (dxCommon) {
            dxCommon->WaitForGpuIdle();
        }

        // DoChangeScene の旧シーン解放と同じ手順で必ず Finalize を呼んでから破棄する。
        // Finalize を経ずに reset すると、シーンが外部システムへ登録したもの
        // （EditorSettingsSubsystem のセクション・Feature の解放処理など）が
        // 解除されないまま破棄され、終了処理でダングリングポインタになる
        // 読み込み途中のシーンも畳んでから本体を解放する
        loadContinuation_ = nullptr;
        loadStepProgress_ = nullptr;
        loadSequence_.reset();
        if (pendingScene_) {
            pendingScene_->Finalize();
            pendingScene_.reset();
        }

        if (currentScene_) {
            if (auto* pipeline = engine_->GetRenderPipeline()) {
                pipeline->RemovePassesByOwner(currentScene_.get());
            }
            currentScene_->Finalize();
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

    Camera* SceneManager::GetGameViewCamera3D() const {
        return currentScene_ ? currentScene_->GetGameViewCamera3D() : nullptr;
    }

    Camera* SceneManager::GetGameViewCamera2D() const {
        return currentScene_ ? currentScene_->GetGameViewCamera2D() : nullptr;
    }

    GameObjectManager* SceneManager::GetCurrentGameObjectManager() const {
        return currentScene_ ? currentScene_->GetGameObjectManager() : nullptr;
    }

    std::vector<RenderViewRequest> SceneManager::BuildRenderViewRequests()
    {
        return currentScene_ ? currentScene_->BuildRenderViewRequests() : std::vector<RenderViewRequest>{};
    }

    void SceneManager::DoChangeScene(const std::string& name) {
        if (!BeginSceneLoad(name)) {
            return;
        }

        // フレームを回さない経路なので、続きはその場で走り切らせる
        loadRunsSynchronously_ = true;
        while (IsSceneLoadInProgress()) {
            StepSceneLoad();
        }
        loadRunsSynchronously_ = false;
    }

    bool SceneManager::BeginSceneLoad(const std::string& name) {
        auto it = sceneFactories_.find(name);
        if (it == sceneFactories_.end()) {
            return false;
        }

        // 実行を始めた列へはステップを足せないので、シーン実体を先に作ってから列を組む
        pendingScene_ = it->second();
        pendingSceneName_ = name;
        pendingScene_->SetSceneManager(this);

        loadContinuation_ = nullptr;
        loadStepProgress_ = nullptr;
        loadSequence_ = std::make_unique<StartupSequence>();
        loadSequence_->Add("旧シーンの解放", [this] { ReleaseCurrentScene(); });
        pendingScene_->BuildLoadTasks(*loadSequence_, engine_);
        loadSequence_->Add("シーンの登録", [this] { AttachPendingScene(); });
        return true;
    }

    void SceneManager::StepSceneLoad() {
        if (!loadSequence_) {
            return;
        }

        if (loadContinuation_) {
            // 続きがあるステップ。完了を返すまで次へ進まない
            ++loadContinuationFrames_;
            if (!loadContinuation_()) {
                return;
            }

            const double seconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - loadContinuationStart_).count();
            Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System,
                "[SceneLoad] {} : {:.3f}s / {} フレーム",
                loadContinuationLabel_, seconds, loadContinuationFrames_);

            loadContinuation_ = nullptr;
            loadStepProgress_ = nullptr;
        } else {
            loadSequence_->Step();
        }

        if (!loadSequence_->HasNext() && !loadContinuation_) {
            loadSequence_.reset();
        }
    }

    void SceneManager::SetLoadStepContinuation(std::function<bool()> work,
                                               std::function<float()> progress) {
        if (loadRunsSynchronously_) {
            // 完了までその場で回す。ワーカーの完了待ちで CPU を占有しないよう間隔を空ける
            while (work && !work()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return;
        }

        loadContinuation_ = std::move(work);
        loadStepProgress_ = std::move(progress);
        loadContinuationLabel_ = loadSequence_ ? loadSequence_->GetNextLabel() : std::string();
        loadContinuationStart_ = std::chrono::steady_clock::now();
        loadContinuationFrames_ = 0;
    }

    float SceneManager::GetSceneLoadProgress() const {
        if (!loadSequence_) {
            return 1.0f;
        }

        const size_t total = loadSequence_->GetTotalCount();
        if (total == 0) {
            return 1.0f;
        }

        float completed = static_cast<float>(loadSequence_->GetCompletedCount());
        if (loadContinuation_ && loadStepProgress_) {
            // 続きの進捗は、それを積んだステップ 1 つ分の幅へ写す
            completed += std::clamp(loadStepProgress_(), 0.0f, 1.0f) - 1.0f;
        }
        return std::clamp(completed / static_cast<float>(total), 0.0f, 1.0f);
    }

    void SceneManager::ReleaseCurrentScene() {
        // GPUの処理完了を待機してから古いシーンを解放
        auto dxCommon = engine_->GetService<GraphicsCore>();
        if (dxCommon) {
            dxCommon->WaitForGpuIdle();
        }

        if (currentScene_) {
            // シーンが登録したユーザーレンダーパスを一括除去
            if (auto* pipeline = engine_->GetRenderPipeline()) {
                pipeline->RemovePassesByOwner(currentScene_.get());
            }

            currentScene_->Finalize();
        }

        currentScene_.reset();
        currentSceneName_ = "None";

        // シーン切り替え時にライトをクリア
        auto lightManager = engine_->GetService<LightManager>();
        if (lightManager) {
            lightManager->ClearAllLights();
        }

        // FPS計測をリセット（シーン切り替え時の異常値を防ぐ）
        auto frameRateController = engine_->GetService<FrameRateController>();
        if (frameRateController) {
            frameRateController->ResetFPSMeasurement();
        }
    }

    void SceneManager::AttachPendingScene() {
        currentScene_ = std::move(pendingScene_);
        currentSceneName_ = pendingSceneName_;

        // シーン固有レンダーパスの登録（所有者タグ付きで、シーン破棄時に自動除去される）
        if (auto* pipeline = engine_->GetRenderPipeline()) {
            pipeline->BeginOwnerScope(currentScene_.get());
            currentScene_->RegisterRenderPasses(*pipeline);
            pipeline->EndOwnerScope();
        }
    }
}
