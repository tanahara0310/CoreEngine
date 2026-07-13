#include "pch.h"
#include "BaseScene.h"
#include "EngineSystem/EngineSystem.h"
#include "Camera/CameraManager.h"
#include "Camera/Debug/DebugCamera.h"
#include "Camera/Camera.h"
#include "Camera/Camera2D.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Model/Model.h"
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
        if (cameraManager_) {
            cameraManager_->Update();
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
        ICamera* activeCamera3D = cameraManager_->GetActiveCamera(CameraType::Camera3D);

        if (!renderManager || !activeCamera3D) {
            return;
        }

        // カメラマネージャーを設定（タイプ別カメラを自動選択）
        renderManager->SetCameraManager(cameraManager_.get());

        // 全てのゲームオブジェクトを描画キューに追加
        gameObjectManager_.RegisterAllToRender(renderManager);
    }

    void BaseScene::Draw()
    {
        if (auto* renderManager = engine_->GetComponent<RenderManager>()) {
            renderManager->SetActiveTransformSlot(TransformBufferSlot::Game);
            renderManager->SetDebugLineRenderingEnabled(true);
        }
        Model::SetCurrentRenderSlot(TransformBufferSlot::Game);
        DrawWithCamera(ResolveGameViewCameraName());
    }

    ICamera* BaseScene::GetDefaultGameViewCamera3D() const
    {
        if (!cameraManager_) {
            return nullptr;
        }

        if (!gameViewCameraName_.empty()) {
            if (ICamera* gameCamera = cameraManager_->GetCamera(gameViewCameraName_)) {
                return gameCamera;
            }
        }

        return cameraManager_->GetGameViewCameraOverride().empty()
            ? cameraManager_->GetActiveCamera(CameraType::Camera3D)
            : cameraManager_->GetCamera(gameViewCameraName_);
    }

    ICamera* BaseScene::GetGameViewCamera3D() const
    {
        if (!cameraManager_) {
            return nullptr;
        }

        if (ICamera* gameCamera = cameraManager_->GetCamera(ResolveGameViewCameraName())) {
            return gameCamera;
        }

        return cameraManager_->GetActiveCamera(CameraType::Camera3D);
    }

    ICamera* BaseScene::GetGameViewCamera2D() const
    {
        return cameraManager_ ? cameraManager_->GetActiveCamera(CameraType::Camera2D) : nullptr;
    }

    void BaseScene::DrawWithCamera(const std::string& cameraName)
    {
        auto renderManager = engine_->GetComponent<RenderManager>();
        auto dxCommon = engine_->GetComponent<DirectXCommon>();
        if (!cameraManager_) {
            return;
        }

        const std::string previousCameraName = cameraManager_->GetActiveCameraName(CameraType::Camera3D);
        const bool shouldSwitchCamera = !cameraName.empty() && cameraName != previousCameraName;

        if (shouldSwitchCamera) {
            cameraManager_->SetActiveCamera(cameraName, CameraType::Camera3D);
        }

        ICamera* activeCamera3D = cameraManager_->GetActiveCamera(CameraType::Camera3D);

        if (!renderManager || !dxCommon || !activeCamera3D) {
            if (shouldSwitchCamera && !previousCameraName.empty()) {
                cameraManager_->SetActiveCamera(previousCameraName, CameraType::Camera3D);
            }
            return;
        }

        ID3D12GraphicsCommandList* cmdList = dxCommon->GetCommandList();
        renderManager->SetCommandList(cmdList);

        renderManager->DrawGeometryPass();

        if (shouldSwitchCamera && !previousCameraName.empty()) {
            cameraManager_->SetActiveCamera(previousCameraName, CameraType::Camera3D);
        }
    }

    void BaseScene::Finalize()
    {
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

        // デバッグカメラを作成して登録
        auto debugCamera = std::make_unique<DebugCamera>();
        debugCamera->Initialize(engine_, dxCommon->GetDevice());
        {
            auto settings = debugCamera->GetSettings();
            settings.useGameView = true;
            debugCamera->SetSettings(settings);
        }
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
        if (auto* releaseCamera = dynamic_cast<Camera*>(cameraManager_->GetCamera("Release"))) {
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
