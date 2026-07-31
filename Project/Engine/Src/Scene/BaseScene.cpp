#include "pch.h"
#include "BaseScene.h"
#include "EngineSystem/EngineSystem.h"
#include "Camera/CameraManager.h"
#include "Camera/Camera.h"
#ifdef USE_IMGUI
#include "Camera/Debug/DebugCameraSettingsSection.h"
#include "EngineSystem/Settings/EditorSettingsSubsystem.h"
#endif
#include "Editor/Camera/EditorCameraInput.h"
#include "Utility/FrameRate/FrameRateController.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Render/RenderManager.h"
#include "Particle/ParticleSystem.h"
#include "Particle/Gpu/GpuParticleSystem.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Scene/SceneManager.h"
#include "Scene/Feature/LightingFeature.h"
#include "Scene/Feature/EnvironmentFeature.h"
#include "Scene/Feature/CollisionFeature.h"
#include "Scene/Feature/GridFeature.h"
#include "Scene/Feature/DebugEditorFeature.h"
#include "Scene/Feature/SceneBGMFeature.h"
#include "Utility/Logger/Logger.h"
#include <algorithm>


namespace CoreEngine
{
    IParticleSystem* BaseScene::CreateParticleSystem(ParticleBackend backend, const std::string& name)
    {
        auto dxCommon = engine_->GetComponent<DirectXCommon>();
        auto resourceFactory = engine_->GetComponent<ResourceFactory>();
        if (!dxCommon || !resourceFactory) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::General,
                "BaseScene::CreateParticleSystem: DirectXCommon / ResourceFactory が未登録のため生成できません");
            return nullptr;
        }

        if (backend == ParticleBackend::GPU) {
            auto* system = CreateObject<GpuParticleSystem>();
            system->Initialize(dxCommon, resourceFactory, name);
            return system;
        }

        auto* system = CreateObject<ParticleSystem>();
        system->Initialize(dxCommon, resourceFactory, name);
        return system;
    }

    void BaseScene::Initialize(EngineSystem* engine)
    {
        engine_ = engine;

        // シーン保存システム
        sceneSaveSystem_ = std::make_unique<SceneSaveSystem>();

        //カメラ
        SetupCamera();

        // 既定 Feature（ライト・グリッド・デバッグエディタ・コリジョン・環境・BGM）の登録と初期化
        RegisterDefaultFeatures();
        RefreshFeatureContext();
        for (auto& entry : features_) {
            entry.feature->Initialize(featureContext_);
        }
        featuresInitialized_ = true;

        // 既定ディレクショナルライトを従来の protected メンバーとして派生クラスへ公開する
        directionalLight_ = lightingFeature_ ? lightingFeature_->GetDirectionalLight() : nullptr;

        // 派生クラス固有の初期化（オブジェクト生成など）
        OnInitialize();

        // OnInitialize() 完了後の Feature フック
        // （シーン生成済みオブジェクトを見る SkyBox / 無限床の採用判定など）
        if (environmentFeature_) {
            environmentFeature_->SetWantsDefaultGround(WantsDefaultGround());
        }
        RefreshFeatureContext();
        for (auto& entry : features_) {
            entry.feature->PostSceneInitialize(featureContext_);
        }

        // 全オブジェクト生成後にシーンデータを JSON から自動復元
        LoadObjectsFromJson();
    }

    void BaseScene::Update()
    {
        // カメラの更新
        // 入力の正規化（ImGui / InputManager 依存）は EditorCameraInput に閉じており、
        // コントローラは CameraInputState しか見ない。
        if (cameraManager_) {
            float deltaTime = 1.0f / 60.0f;
            if (auto* frameRate = engine_->GetComponent<FrameRateController>()) {
                deltaTime = frameRate->GetDeltaTime();
            }
            cameraManager_->Update(EditorCameraInput::Collect(engine_), deltaTime);
        }

        // フレーム前処理（ライト/影・グリッド・デバッグエディタ）
        DispatchUpdate(SceneUpdatePhase::FrameStart);

        // 派生クラスの更新処理（GameObjectの更新前）
        OnUpdate();

        // GameObject 更新前の Feature 更新（床のカメラ追従など位置の事前確定）
        DispatchUpdate(SceneUpdatePhase::PreObjectUpdate);

        // ゲームオブジェクトの更新
        gameObjectManager_.UpdateAll();

        // GameObject 更新後の Feature 更新（コリジョン収集 → 判定など）
        DispatchUpdate(SceneUpdatePhase::PostObjectUpdate);

        // 派生クラスの後処理（クリーンアップ前）
        OnLateUpdate();

        // 全ロジック確定後の Feature 更新（大気→雲など最新の太陽・カメラ情報の反映）
        DispatchUpdate(SceneUpdatePhase::PostLogic);
    }

    void BaseScene::PrepareRender()
    {
        auto renderManager = engine_->GetComponent<RenderManager>();
        Camera* activeCamera3D = cameraManager_->GetActiveCamera(CameraType::Camera3D);

        if (!renderManager || !activeCamera3D) {
            return;
        }

        // 全てのゲームオブジェクトを描画キューに追加
        gameObjectManager_.RegisterAllToRender(renderManager);
    }

    void BaseScene::Draw()
    {
        auto* renderManager = engine_->GetComponent<RenderManager>();
        auto* dxCommon = engine_->GetComponent<DirectXCommon>();
        if (!renderManager || !dxCommon) {
            return;
        }

        // 描画に使うビューは EngineSystem がフレーム先頭で確定済み（FrameViews）。
        // 以前はここでアクティブカメラを一時的に差し替えて描き、元へ戻していたが、
        // その間だけ「アクティブカメラ」の意味が変わるため、ギズモ／ピッキングが
        // 描画とは別のカメラを見てズレる原因になっていた。
        renderManager->SetDebugLineRenderingEnabled(true);
        renderManager->SetCommandList(dxCommon->GetCommandList());
        renderManager->DrawGeometryPass();
    }

    Camera* BaseScene::GetDefaultGameViewCamera3D() const
    {
        if (!cameraManager_) {
            return nullptr;
        }

        if (!gameViewCameraName_.empty()) {
            if (Camera* gameCamera = cameraManager_->GetCamera(gameViewCameraName_)) {
                return gameCamera;
            }
        }

        return cameraManager_->GetGameViewCameraOverride().empty()
            ? cameraManager_->GetActiveCamera(CameraType::Camera3D)
            : cameraManager_->GetCamera(gameViewCameraName_);
    }

    Camera* BaseScene::GetGameViewCamera3D() const
    {
        if (!cameraManager_) {
            return nullptr;
        }

        if (Camera* gameCamera = cameraManager_->GetCamera(ResolveGameViewCameraName())) {
            return gameCamera;
        }

        return cameraManager_->GetActiveCamera(CameraType::Camera3D);
    }

    Camera* BaseScene::GetGameViewCamera2D() const
    {
        return cameraManager_ ? cameraManager_->GetActiveCamera(CameraType::Camera2D) : nullptr;
    }

    void BaseScene::Finalize()
    {
#ifdef USE_IMGUI
        // エディタ設定セクションの解除（解除時に最終保存が走る）。カメラ破棄より先に行うこと
        if (debugCameraSettingsSection_) {
            if (auto* editorSettings = engine_->GetSubsystem<EditorSettingsSubsystem>()) {
                editorSettings->UnregisterSections(this);
            }
            debugCameraSettingsSection_.reset();
        }
#endif

        // 派生クラス固有の解放
        OnFinalize();

        // Feature の解放（登録の逆順）
        RefreshFeatureContext();
        for (auto it = features_.rbegin(); it != features_.rend(); ++it) {
            it->feature->Finalize(featureContext_);
        }

        // ゲームオブジェクトをクリア（新システム）
        gameObjectManager_.Clear();

        // Feature を破棄（委譲先ポインタも無効化）
        features_.clear();
        featuresInitialized_ = false;
        lightingFeature_ = nullptr;
        environmentFeature_ = nullptr;
        collisionFeature_ = nullptr;
        bgmFeature_ = nullptr;
        directionalLight_ = nullptr;
    }

    ISceneFeature* BaseScene::AddFeature(std::unique_ptr<ISceneFeature> feature, int priority)
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

    void BaseScene::RegisterDefaultFeatures()
    {
        // 登録順 = 同 priority 内の実行順。従来 BaseScene::Update の暗黙順序を再現する
        auto lighting = std::make_unique<LightingFeature>();
        lightingFeature_ = lighting.get();
        AddFeature(std::move(lighting));

#ifdef USE_IMGUI
        AddFeature(std::make_unique<GridFeature>());
        AddFeature(std::make_unique<DebugEditorFeature>());
#endif

        auto collision = std::make_unique<CollisionFeature>();
        collisionFeature_ = collision.get();
        AddFeature(std::move(collision));

        auto environment = std::make_unique<EnvironmentFeature>();
        environmentFeature_ = environment.get();
        AddFeature(std::move(environment));

        auto bgm = std::make_unique<SceneBGMFeature>();
        bgmFeature_ = bgm.get();
        AddFeature(std::move(bgm));
    }

    void BaseScene::RefreshFeatureContext()
    {
        featureContext_.engine = engine_;
        featureContext_.gameObjectManager = &gameObjectManager_;
        featureContext_.cameraManager = cameraManager_.get();
        featureContext_.sceneManager = sceneManager_;
        featureContext_.saveSystem = sceneSaveSystem_.get();
        featureContext_.gameViewCamera3D = GetGameViewCamera3D();
    }

    void BaseScene::DispatchUpdate(SceneUpdatePhase phase)
    {
        RefreshFeatureContext();
        for (auto& entry : features_) {
            entry.feature->Update(featureContext_, phase);
        }
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
        // y は既定の無限遠タイル床（y=0）より上に置く。床の高さにカメラがあると
        // 足元の床がニアクリップで消え、地平線より下に大気の下向き（＝黒）が見えてしまう。
        auto releaseCamera = std::make_unique<Camera>();
        releaseCamera->Initialize(dxCommon->GetDevice());
        releaseCamera->SetTranslate({ 0.0f, kDefaultCameraHeight, -30.0f });
        releaseCamera->SetRotate({ 0.0f, 0.0f, 0.0f });

        cameraManager_->RegisterCamera("Release", std::move(releaseCamera));

        // デバッグ用カメラ（カメラ自体は Release と同じ型。Blender 風の操作は
        // OrbitFlyController を取り付けることで与える）
        auto debugCamera = std::make_unique<Camera>();
        debugCamera->Initialize(dxCommon->GetDevice());
        cameraManager_->RegisterCamera("Debug", std::move(debugCamera));

        cameraManager_->SetEngineSystem(engine_);
        OrbitFlyController* orbitController = cameraManager_->AttachController<OrbitFlyController>("Debug");

        // Release カメラは一人称の自由移動で操作する（旧 GameView 操作モジュール相当）
        cameraManager_->AttachController<FreeLookController>("Release");

#ifdef USE_IMGUI
        // エディタ設定の自動保存: 登録時に前回終了時の姿勢・設定が復元され、以降の変更が
        // 自動保存される
        if (auto* editorSettings = engine_->GetSubsystem<EditorSettingsSubsystem>()) {
            debugCameraSettingsSection_ = std::make_unique<DebugCameraSettingsSection>(
                cameraManager_->GetCamera("Debug"), orbitController);
            editorSettings->RegisterSection(debugCameraSettingsSection_.get(), this);
        }
#else
        (void)orbitController;
#endif

        // デフォルトでリリースカメラをアクティブに設定
        cameraManager_->SetActiveCamera("Release", CameraType::Camera3D);

        // ===== 2Dカメラの設定 =====

        // 2Dカメラ = 正射影パラメータを持つ同じ Camera（画面中央が原点）
        auto camera2D = std::make_unique<Camera>(CameraParameters::Orthographic2D());
        camera2D->SetTranslate({ 0.0f, 0.0f, 0.0f });
        camera2D->SetZoom(1.0f);
        camera2D->Initialize(nullptr); // 2D は GPU 定数バッファ不要

        cameraManager_->RegisterCamera("Camera2D", std::move(camera2D));

        // 2Dカメラをアクティブに設定
        cameraManager_->SetActiveCamera("Camera2D", CameraType::Camera2D);
    }

    std::string BaseScene::ResolveGameViewCameraName() const
    {
        if (!cameraManager_) {
            return {};
        }

        // CameraManager 側のオーバーライドが有効であれば最優先で使用する
        const std::string& overrideName = cameraManager_->GetGameViewCameraOverride();
        if (!overrideName.empty() && cameraManager_->GetCamera(overrideName)) {
            return overrideName;
        }

        if (!gameViewCameraName_.empty() && cameraManager_->GetCamera(gameViewCameraName_)) {
            return gameViewCameraName_;
        }

        return cameraManager_->GetActiveCameraName(CameraType::Camera3D);
    }

    void BaseScene::SetReleaseCameraTransform(const Vector3& translate, const Vector3& rotate)
    {
        if (!cameraManager_) {
            return;
        }
        if (auto* releaseCamera = cameraManager_->GetCamera("Release")) {
            releaseCamera->SetTranslate(translate);
            releaseCamera->SetRotate(rotate);
        }
    }

    void BaseScene::SetCollisionEnabled(CollisionLayer a, CollisionLayer b, bool enable)
    {
        if (collisionFeature_) {
            collisionFeature_->SetCollisionEnabled(a, b, enable);
        }
    }

    SkyBoxObject* BaseScene::GetSkyBox() const
    {
        return environmentFeature_ ? environmentFeature_->GetSkyBox() : nullptr;
    }

    void BaseScene::RegisterSceneBGM(std::unique_ptr<SoundManager::SoundResource>* bgm)
    {
        if (bgmFeature_) {
            RefreshFeatureContext();
            bgmFeature_->RegisterSceneBGM(featureContext_, bgm);
        }
    }

    void BaseScene::LoadObjectsFromJson()
    {
        sceneSaveSystem_->Load(&gameObjectManager_);
    }

    void BaseScene::SaveObjectsToJson()
    {
        sceneSaveSystem_->SaveScene(&gameObjectManager_);
    }

    void BaseScene::SaveSingleObjectToJson(GameObject* obj)
    {
        sceneSaveSystem_->SaveObject(obj);
    }
}
