#include "pch.h"
#include "Scene.h"
#include "EngineSystem/EngineSystem.h"
#include "EngineSystem/PlaybackState.h"
#include "Camera/CameraManager.h"
#include "Camera/Camera.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/Render/RenderManager.h"
#include "Scene/SceneManager.h"
#include "Scene/SceneEnvironmentIO.h"
#include "Graphics/Model/ModelManager.h"
#include "Scene/Feature/DefaultSceneFeatures.h"
// GetFeature<T>() で引くために完全型が必要な既定 Feature だけを include する
// （顔ぶれそのものは DefaultSceneFeatures.cpp が持つ）
#include "Scene/Feature/CameraFeature.h"
#include "Scene/Feature/LightingFeature.h"
#include "Scene/Feature/GroundFeature.h"
#include "Scene/Feature/CollisionFeature.h"
#include "Scene/Feature/SceneFeatureRegistry.h"
#include "Utility/Logger/Logger.h"
#include <algorithm>
#include <utility>


namespace CoreEngine
{
    Scene::Scene(std::string sceneName)
        : sceneName_(std::move(sceneName))
    {
    }

    void Scene::BuildLoadTasks(StartupSequence& sequence, EngineSystem* engine)
    {
        // Feature の後処理は保存データの復元より後に置く。シーンのオブジェクトを見て
        // 決める Feature（空の採用判定・既定床の生成）が、コードで作ったオブジェクトと
        // 保存データで置いたオブジェクトの両方を見られるようにするため。
        sequence.Add("カメラと Feature の登録", [this, engine] { SetupSceneCore(engine); });
        sequence.Add("Feature の初期化", [this] { InitializeFeatures(); });
        sequence.Add("シーンの設定", [this] { ApplyManifestSettings(); });
        sequence.Add("モデルの先読み", [this] { BeginModelPreload(); });
        sequence.Add("シーンデータの復元", [this] { BeginSceneDataRestore(); });
        sequence.Add("Feature の後処理", [this] { RunPostSceneInitialize(); });
    }

    void Scene::SetupSceneCore(EngineSystem* engine)
    {
        engine_ = engine;

        // シーン保存システム（控えから組み直すときは、その控えから読む）
        sceneSaveSystem_ = std::make_unique<SceneSaveSystem>();
        sceneSaveSystem_->SetSceneName(sceneName_);
        sceneSaveSystem_->SetRestoreSnapshot(restoreSnapshot_);

        // 既定 Feature の登録（顔ぶれは CreateDefaultSceneFeatures() 側）
        RegisterDefaultFeatures();
    }

    void Scene::InitializeFeatures()
    {
        // コンテキストは 1 体ごとに取り直す。CameraFeature が生成したカメラを、
        // 後続の Feature が同じループの中で参照できるようにするため。
        for (auto& entry : features_) {
            RefreshFeatureContext();
            entry.feature->Initialize(featureContext_);
        }
        featuresInitialized_ = true;
    }

    void Scene::ApplyManifestSettings()
    {
        // 見た目（環境とポストエフェクト）はシーンが持つ。前のシーンの画が残らないよう、
        // 一度コード既定へ戻してからこのシーンの保存を当てる
        SceneEnvironmentIO::ResetToDefaults(engine_);
        SceneEnvironmentIO::Load(GetSceneName(), engine_);

        const SceneSaveSystem::ManifestSettings settings =
            SceneSaveSystem::LoadManifestSettings(GetSceneName());
        Logger& log = Logger::GetInstance();

        if (settings.defaultGround) {
            SetDefaultGroundEnabled(*settings.defaultGround);
        }

        for (const std::string& name : settings.features) {
            std::unique_ptr<ISceneFeature> feature = SceneFeatureRegistry::Create(name);
            if (!feature) {
                std::string available;
                for (const std::string& registered : SceneFeatureRegistry::GetNames()) {
                    if (!available.empty()) {
                        available += ", ";
                    }
                    available += registered;
                }
                log.Logf(LogLevel::Warn, LogCategory::System,
                    "シーン {} の Feature {} は登録されていないので、足しません（足せるもの: {}）",
                    GetSceneName(), name, available);
                continue;
            }
            if (FindFeature(feature->GetName())) {
                log.Logf(LogLevel::Warn, LogCategory::System,
                    "シーン {} の Feature {} は既にあるので、足しません", GetSceneName(), name);
                continue;
            }
            AddFeature(std::move(feature));
            log.Logf(LogLevel::Info, LogCategory::System,
                "シーン {} に Feature {} を足しました", GetSceneName(), name);
        }

        if (settings.collisionPairs) {
            ApplyCollisionPairs(*settings.collisionPairs);
        }
    }

    void Scene::ApplyCollisionPairs(const std::vector<std::pair<std::string, std::string>>& pairs)
    {
        auto* const collision = GetFeature<CollisionFeature>();
        if (!collision) {
            return;
        }

        // 保存データが全部を決める。書かれていない組み合わせは当たらない
        CollisionConfig& config = collision->GetConfig();
        config.DisableAll();
        for (const auto& [first, second] : pairs) {
            CollisionLayer a{};
            CollisionLayer b{};
            if (!TryParseCollisionLayer(first, a) || !TryParseCollisionLayer(second, b)) {
                Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                    "シーン {} の衝突の組み合わせ「{} と {}」に知らないレイヤーがあるので、飛ばします",
                    GetSceneName(), first, second);
                continue;
            }
            config.SetCollisionEnabled(a, b, true);
        }
    }

    SceneSaveSystem::ManifestSettings Scene::CollectManifestSettings() const
    {
        SceneSaveSystem::ManifestSettings settings;

        // 名前で足せる Feature だけを書く（既定で全シーンに入るものは書かない）
        const std::vector<std::string> registered = SceneFeatureRegistry::GetNames();
        for (const auto& entry : features_) {
            const char* const name = entry.feature->GetName();
            if (name && std::find(registered.begin(), registered.end(), name) != registered.end()) {
                settings.features.emplace_back(name);
            }
        }

        if (const auto* ground = GetFeature<GroundFeature>()) {
            settings.defaultGround = !ground->IsSuppressed();
        }

        if (auto* const collision = GetFeature<CollisionFeature>()) {
            const CollisionConfig& config = collision->GetConfig();
            std::vector<std::pair<std::string, std::string>> pairs;
            // 対称なので下三角だけを書く
            for (int row = 0; row < CollisionConfig::kMaxLayers; ++row) {
                for (int col = 0; col <= row; ++col) {
                    const auto a = static_cast<CollisionLayer>(row);
                    const auto b = static_cast<CollisionLayer>(col);
                    if (config.IsCollisionEnabled(a, b)) {
                        pairs.emplace_back(ToString(a), ToString(b));
                    }
                }
            }
            settings.collisionPairs = std::move(pairs);
        }

        return settings;
    }

    void Scene::SaveSceneSettings()
    {
        if (!sceneSaveSystem_ || GetSceneName().empty()) {
            return;
        }
        sceneSaveSystem_->SaveManifestSettings(CollectManifestSettings());
        SceneEnvironmentIO::Save(GetSceneName());
    }

    void Scene::RunPostSceneInitialize()
    {
        // シーンのオブジェクトが出そろった後の Feature フック
        // （SkyBox の採用判定・既定床の生成・カメラの構図の復元）
        RefreshFeatureContext();
        for (auto& entry : features_) {
            entry.feature->PostSceneInitialize(featureContext_);
        }
    }

    void Scene::BeginModelPreload()
    {
        auto* modelManager = engine_ ? engine_->GetService<ModelManager>() : nullptr;
        if (!modelManager || !sceneSaveSystem_ || !sceneManager_) {
            return;
        }

        const std::vector<std::string> modelPaths = restoreSnapshot_
            ? SceneSaveSystem::CollectModelPaths(*restoreSnapshot_)
            : SceneSaveSystem::CollectModelPaths(sceneSaveSystem_->GetSceneName());
        if (modelPaths.empty()) {
            return;
        }

        // ワーカーへ投げて即座に戻る。読み終わるまでの各フレームで画面は回り続ける
        modelManager->BeginPreload(modelPaths);

        sceneManager_->SetLoadStepContinuation(
            [modelManager] {
                const auto progress = modelManager->GetPreloadProgress();
                return progress.first >= progress.second;
            },
            [modelManager] {
                const auto progress = modelManager->GetPreloadProgress();
                return (progress.second == 0)
                    ? 1.0f
                    : static_cast<float>(progress.first) / static_cast<float>(progress.second);
            });
    }

    void Scene::BeginSceneDataRestore()
    {
        // 進捗を出す相手（SceneManager）が居ない経路は、その場で読み切る
        if (!sceneManager_) {
            sceneSaveSystem_->Load(&gameObjectManager_);
            return;
        }

        sceneSaveSystem_->BeginLoad(&gameObjectManager_);

        // 1 フレームに 1 体ずつ復元する
        sceneManager_->SetLoadStepContinuation(
            [this] { return sceneSaveSystem_->StepLoad(); },
            [this] { return sceneSaveSystem_->GetLoadProgress(); });
    }

    void Scene::Update(SceneUpdateMode mode)
    {
        // 進行を止める理由は 2 つあり、扱いは同じ。再生していないとき（編集中・一時停止中）と、
        // シーン切り替えのトランジション（mode == Suspended）。どちらもゲームロジックだけを
        // 飛ばす。Feature の取捨は DispatchUpdate() が RunsWhileStopped() を見て行うので、
        // エディタカメラ・ギズモ・ライト・大気は止めている間も回り続ける
        const bool stopped = (mode == SceneUpdateMode::Suspended)
            || !PlaybackStateManager::GetInstance().IsAdvancing();
        const bool advance = !stopped;

        // フレーム前処理（先頭でカメラ姿勢を確定 → ライト/影・グリッド・デバッグエディタ）
        DispatchUpdate(SceneUpdatePhase::FrameStart, stopped);

        // GameObject 更新前の Feature 更新（床のカメラ追従、最後にトゥイーンの前進）
        DispatchUpdate(SceneUpdatePhase::PreObjectUpdate, stopped);

        if (advance) {
            // ゲームオブジェクトの更新（Update の後・LateUpdate の前に BetweenObjectUpdates の Feature を回す）
            gameObjectManager_.UpdateAll([this] { DispatchUpdate(SceneUpdatePhase::BetweenObjectUpdates, false); });
        } else {
            // 停止中はワールド行列の転送だけを残す。
            // インスペクタやギズモで動かした結果を画面へ出すために要る
            gameObjectManager_.SyncTransforms();
        }

        // GameObject 更新後の Feature 更新（コリジョン収集 → 判定、最後にイベントの一括配信）
        DispatchUpdate(SceneUpdatePhase::PostObjectUpdate, stopped);

        // 全ロジック確定後の Feature 更新（大気→雲など最新の太陽・カメラ情報の反映）
        DispatchUpdate(SceneUpdatePhase::PostLogic, stopped);
    }

    void Scene::PrepareRender()
    {
        // cameraManager_ は CameraFeature が GraphicsCore を取れなかった場合に空のままになる。
        // ここでも null を許容すること（描画キューを積まずに抜ける）。
        auto renderManager = engine_->GetService<RenderManager>();
        Camera* activeCamera3D = cameraManager_
            ? cameraManager_->GetActiveCamera(CameraType::Camera3D)
            : nullptr;

        if (!renderManager || !activeCamera3D) {
            return;
        }

        // 全てのゲームオブジェクトを描画キューに追加
        gameObjectManager_.RegisterAllToRender(renderManager);
    }

    Camera* Scene::GetGameViewCamera3D() const
    {
        // 覗いているカメラの決定は CameraManager に一本化されている（Scene / Game の役割 + フラグ）。
        // シーン側で名前を解決し直すと、また規則が二重化して食い違う。
        return cameraManager_ ? cameraManager_->GetViewCamera() : nullptr;
    }

    Camera* Scene::GetGameCamera3D() const
    {
        return cameraManager_ ? cameraManager_->GetGameCamera() : nullptr;
    }

    Camera* Scene::GetGameViewCamera2D() const
    {
        return cameraManager_ ? cameraManager_->GetActiveCamera(CameraType::Camera2D) : nullptr;
    }

    void Scene::Finalize()
    {
        // Feature の解放（登録の逆順）。
        // CameraFeature は先頭に登録されているのでここでは最後に回り、
        // 他の Feature が解放中も ctx.cameraManager を参照できる。
        RefreshFeatureContext();
        for (auto it = features_.rbegin(); it != features_.rend(); ++it) {
            it->feature->Finalize(featureContext_);
        }

        // ゲームオブジェクトをクリア（新システム）
        gameObjectManager_.Clear();

        // GameObject 破棄後の Feature フック（登録の逆順）。
        // 取り残された購読・再生途中のトゥイーンなど、「オブジェクトの破棄で
        // 解除されるはずだったもの」をここで畳む。
        for (auto it = features_.rbegin(); it != features_.rend(); ++it) {
            it->feature->PostSceneFinalize(featureContext_);
        }

        // Feature を破棄（GetFeature<T>() は以降 nullptr を返す）。
        // カメラ一式の実体もここで消えるので、キャッシュを先に落としておく
        cameraManager_ = nullptr;
        features_.clear();
        featuresInitialized_ = false;
    }

    std::vector<const char*> Scene::GetFeatureNames() const
    {
        std::vector<const char*> names;
        names.reserve(features_.size());
        for (const auto& entry : features_) {
            names.push_back(entry.feature->GetName());
        }
        return names;
    }

    ISceneFeature* Scene::FindFeature(std::string_view name) const
    {
        for (const auto& entry : features_) {
            const char* const featureName = entry.feature->GetName();
            if (featureName && name == featureName) {
                return entry.feature.get();
            }
        }
        return nullptr;
    }

    ISceneFeature* Scene::AddFeature(std::unique_ptr<ISceneFeature> feature, int priority)
    {
        if (!feature) {
            return nullptr;
        }

        FeatureEntry entry;
        entry.feature = std::move(feature);
        entry.priority = priority;
        entry.sequence = featureSequence_++;

        // (priority, 登録順) で決まる位置へ挿入し、features_ を常にソート済みに保つ
        // （RenderPipeline::AddPass と同じ規約）
        auto insertPos = std::find_if(features_.begin(), features_.end(),
            [&entry](const FeatureEntry& existing) {
                return existing.priority > entry.priority;
            });

        ISceneFeature* result = entry.feature.get();
        features_.insert(insertPos, std::move(entry));

        // シーン初期化後（OnInitialize() 内など）の追加は即座に初期化する
        if (featuresInitialized_) {
            RefreshFeatureContext();
            result->Initialize(featureContext_);
        }
        return result;
    }

    void Scene::RegisterDefaultFeatures()
    {
        // 顔ぶれと並びは DefaultSceneFeatures.cpp が持つ。
        // 参照が必要になったら GetFeature<T>() で引くので、ここでは持ち回らない。
        for (auto& entry : CreateDefaultSceneFeatures()) {
            AddFeature(std::move(entry.feature), entry.priority);
        }
    }

    void Scene::RefreshFeatureContext()
    {
        // カメラ一式は CameraFeature が所有する。初回だけ型で引き、以降はキャッシュを使う
        // （毎フレーム 4 回以上通るので、ここで走査を繰り返さない）。
        if (!cameraManager_) {
            if (auto* camera = GetFeature<CameraFeature>()) {
                cameraManager_ = camera->GetCameraManager();
            }
        }

        featureContext_.engine = engine_;
        featureContext_.gameObjectManager = &gameObjectManager_;
        featureContext_.cameraManager = cameraManager_;
        featureContext_.sceneManager = sceneManager_;
        featureContext_.saveSystem = sceneSaveSystem_.get();
        featureContext_.gameViewCamera3D = GetGameViewCamera3D();
    }

    void Scene::DispatchUpdate(SceneUpdatePhase phase, bool stopped)
    {
        // 停止中はゲームの進行に関わる Feature を飛ばす。
        // どちらに属するかは Feature 自身が RunsWhileStopped() で答える。
        // 「止まっているか」の判断は Update() が一括で行う（停止ボタンとシーン遷移の両方）

        // コンテキストは 1 体ごとに取り直す。先頭の CameraFeature が視点を切り替えた
        // フレームでも、後続の Feature が同じフレームで新しい視点カメラを見られるようにする。
        for (auto& entry : features_) {
            if (stopped && !entry.feature->RunsWhileStopped()) {
                continue;
            }
            RefreshFeatureContext();
            entry.feature->Update(featureContext_, phase);
        }
    }

    void Scene::SetDefaultGroundEnabled(bool enabled)
    {
        if (auto* ground = GetFeature<GroundFeature>()) {
            ground->SetSuppressed(!enabled);
        }
    }
}
