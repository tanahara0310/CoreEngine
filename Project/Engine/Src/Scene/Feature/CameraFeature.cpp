#include "pch.h"
#include "CameraFeature.h"

#include "Camera/Camera.h"
#include "Camera/CameraSceneStateIO.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "GameObject/Component/Camera/CameraComponent.h"
#include "Scene/SceneSaveSystem.h"
#include "Camera/CameraManager.h"
#include "Camera/Control/CameraInputState.h"
#include "Camera/Debug/DebugCameraState.h"
#ifdef CORE_EDITOR
#include "Editor/Camera/EditorCameraInput.h"
#endif
#include "EngineSystem/EngineSystem.h"
#include "Input/InputManager.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>

namespace CoreEngine
{
    CameraFeature::CameraFeature() = default;
    CameraFeature::~CameraFeature() = default;

    void CameraFeature::Initialize(SceneContext& ctx)
    {
        auto* dxCommon = ctx.engine ? ctx.engine->GetService<GraphicsCore>() : nullptr;
        if (!dxCommon) {
            return;
        }

        // カメラマネージャーを作成
        cameraManager_ = std::make_unique<CameraManager>();

        // ===== 3Dカメラの設定 =====

        // リリースカメラを作成して登録（斜め上から俯瞰する視点）
        // y は既定の無限遠タイル床（y=0）より上に置く。床の高さにカメラがあると
        // 足元の床がニアクリップで消え、地平線より下に大気の下向き（＝黒）が見えてしまう。
        auto gameCamera = std::make_unique<Camera>();
        gameCamera->Initialize(dxCommon->GetDevice());
        gameCamera->SetTranslate({ 0.0f, kDefaultCameraHeight, -30.0f });
        gameCamera->SetRotate({ 0.0f, 0.0f, 0.0f });

        cameraManager_->RegisterCamera(CameraNames::Game, std::move(gameCamera));

        // エディタ視点カメラ（カメラ自体は Game と同じ型。Blender 風の操作は
        // OrbitFlyController を取り付けることで与える）
        auto sceneCamera = std::make_unique<Camera>();
        sceneCamera->Initialize(dxCommon->GetDevice());
        cameraManager_->RegisterCamera(CameraNames::Scene, std::move(sceneCamera));

        cameraManager_->SetEngineSystem(ctx.engine);
        OrbitFlyController* orbitController =
            cameraManager_->AttachController<OrbitFlyController>(CameraNames::Scene);

        // ゲーム視点カメラは一人称の自由移動で操作する（既定は無効）
        cameraManager_->AttachController<FreeLookController>(CameraNames::Game);

        // 起動時はゲーム視点で覗く（エディタ視点への切り替えはキー 1 / カメラUI）
        cameraManager_->SetUseSceneCamera(false);

        // エディタ視点カメラは、生成直後のこの時点で控えの設定・姿勢を当て、以降は毎フレーム控え直す
        sceneCamera_ = cameraManager_->GetCamera(CameraNames::Scene);
        orbitController_ = orbitController;
        if (sceneCamera_ && orbitController_) {
            DebugCameraState::RestoreTo(*sceneCamera_, *orbitController_);
        }

        // ===== 2Dカメラの設定 =====

        // 2Dカメラ = 正射影パラメータを持つ同じ Camera（画面中央が原点）
        auto camera2D = std::make_unique<Camera>(CameraParameters::Orthographic2D());
        camera2D->SetTranslate({ 0.0f, 0.0f, 0.0f });
        camera2D->SetZoom(1.0f);
        camera2D->Initialize(nullptr); // 2D は GPU 定数バッファ不要

        cameraManager_->RegisterCamera(CameraNames::Camera2D, std::move(camera2D));
        cameraManager_->SetActiveCamera(CameraNames::Camera2D, CameraType::Camera2D);
    }

    void CameraFeature::PostSceneInitialize(SceneContext& ctx)
    {
        if (!cameraManager_ || !ctx.saveSystem) {
            return;
        }

        // シーンに置かれたカメラを先に実体にする（保存ファイルが名前で指せるようにする）
        SyncSceneCameras(ctx);

        // 保存が無ければ何もしない。エディタ視点は控えの値がそのまま残る。
        CameraSceneStateIO::Load(ctx.saveSystem->GetSceneName(), *cameraManager_);

        // シーンに置かれたカメラが勝つ（保存ファイルの指定より後に当てる）
        ApplyMainCamera();
    }

    void CameraFeature::Update([[maybe_unused]] SceneContext& ctx, SceneUpdatePhase phase)
    {
        if (phase != SceneUpdatePhase::FrameStart || !cameraManager_) {
            return;
        }

        // 入力の正規化（ImGui / InputManager 依存）は EditorCameraInput に閉じており、
        // コントローラは CameraInputState しか見ない（エディタを含まないビルドは入力なし）。
        // カメラ操作はポーズやスローの影響を受けない
#ifdef CORE_EDITOR
        const CameraInputState input = EditorCameraInput::Collect(ctx.engine);
#else
        const CameraInputState input = CameraInputState::None();
#endif
        cameraManager_->Update(input, Time::UnscaledDeltaTime());

        // シーンに置かれたカメラは、オブジェクトの Transform とレンズが正本。
        // コントローラの反映より後に写して、この 1 フレームの姿勢を確定させる
        SyncSceneCameras(ctx);
        ApplyMainCamera();

        // 更新後の設定・姿勢を控える（カメラ UI・マウス操作のどちらの変更も拾う）
        CaptureEditorCamera();
    }

    void CameraFeature::SyncSceneCameras(SceneContext& ctx)
    {
        mainCameraName_.clear();
        if (!cameraManager_ || !ctx.gameObjectManager) {
            return;
        }

        auto* const dxCommon = ctx.engine ? ctx.engine->GetService<GraphicsCore>() : nullptr;
        std::vector<std::string> alive;

        // 無効なコンポーネント・非アクティブなオブジェクトのカメラも実体を残す必要があるので、
        // ForEachComponent（有効なものだけを回す）ではなく自分で走査する
        for (const auto& object : ctx.gameObjectManager->GetAllObjects()) {
            if (!object || object->IsMarkedForDestroy()) {
                continue;
            }
            for (const auto& slot : object->GetAllComponents()) {
                auto* const component = dynamic_cast<CameraComponent*>(slot.get());
                if (!component) {
                    continue;
                }

                const std::string& name = object->GetName();
                if (name == CameraNames::Game || name == CameraNames::Scene
                    || name == CameraNames::Camera2D) {
                    Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                        "カメラのオブジェクト名 {} はエンジンが使っているので、実体を作りません",
                        name);
                    continue;
                }

                if (component->GetRegisteredName() != name) {
                    // 初めて見つけた、または名前が変わった。実体を作り直す
                    if (!component->GetRegisteredName().empty()) {
                        cameraManager_->UnregisterCamera(component->GetRegisteredName());
                    }
                    auto created = std::make_unique<Camera>();
                    created->Initialize(dxCommon ? dxCommon->GetDevice() : nullptr);
                    Camera* const raw = created.get();
                    cameraManager_->RegisterCamera(name, std::move(created));
                    cameraManager_->SetObjectOwnedCamera(name, true);
                    component->SetRegisteredName(name);
                    component->SetCamera(raw);
                }

                if (Camera* const camera = component->GetCamera()) {
                    component->ApplyTo(*camera);
                    camera->UpdateMatrix();
                }
                alive.push_back(name);

                if (mainCameraName_.empty() && component->IsMainCamera()
                    && component->IsEnabled() && object->IsActive()) {
                    mainCameraName_ = name;
                }
            }
        }

        // 消えたオブジェクトのカメラを外す
        for (const std::string& name : sceneCameraNames_) {
            if (std::find(alive.begin(), alive.end(), name) == alive.end()) {
                cameraManager_->UnregisterCamera(name);
            }
        }
        sceneCameraNames_ = std::move(alive);
    }

    void CameraFeature::ApplyMainCamera()
    {
        if (!cameraManager_) {
            return;
        }

        if (!mainCameraName_.empty()) {
            cameraManager_->SetGameCameraName(mainCameraName_);
            return;
        }

        // シーンのカメラが無くなったらエンジン既定のカメラへ戻す
        //（消えたカメラを指したままだとゲームビューが映らなくなる）
        if (!cameraManager_->GetGameCamera()) {
            cameraManager_->SetGameCameraName(CameraNames::Game);
        }
    }

    void CameraFeature::Finalize(SceneContext&)
    {
        // 最後の設定・姿勢を控えておく（最終フレームの Update 以降の変更を取りこぼさない）。
        // カメラの破棄より先に行うこと
        CaptureEditorCamera();

        sceneCamera_ = nullptr;
        orbitController_ = nullptr;
    }

    void CameraFeature::CaptureEditorCamera()
    {
        if (sceneCamera_ && orbitController_) {
            DebugCameraState::Capture(*sceneCamera_, *orbitController_);
        }
    }

    void CameraFeature::SetReleaseCameraTransform(const Vector3& translate, const Vector3& rotate)
    {
        if (!cameraManager_) {
            return;
        }
        if (auto* releaseCamera = cameraManager_->GetCamera(CameraNames::Game)) {
            releaseCamera->SetTranslate(translate);
            releaseCamera->SetRotate(rotate);
        }
    }

    void CameraFeature::SetReleaseCameraLens(float fovDegrees, float farClip, float nearClip)
    {
        if (!cameraManager_) {
            return;
        }
        if (auto* releaseCamera = cameraManager_->GetCamera(CameraNames::Game)) {
            CameraParameters params = releaseCamera->GetParameters();
            params.SetFovDegrees(fovDegrees);
            params.nearClip = nearClip;
            params.farClip = farClip;
            releaseCamera->SetParameters(params);
        }
    }
}
