#include "pch.h"
#include "CameraFeature.h"

#include "Camera/Camera.h"
#include "Camera/CameraSceneStateIO.h"
#include "Scene/SceneSaveSystem.h"
#include "Camera/CameraManager.h"
#include "Camera/Debug/DebugCameraCVars.h"
#include "Editor/Camera/EditorCameraInput.h"
#include "EngineSystem/EngineSystem.h"
#include "Input/InputManager.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Utility/FrameRate/Time.h"

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

        // エディタ視点カメラの設定・姿勢を CVar 経由で永続化する。
        // 生成直後のこの時点で前回終了時の状態を復元し、以降は毎フレーム CVar へ写す
        sceneCamera_ = cameraManager_->GetCamera(CameraNames::Scene);
        orbitController_ = orbitController;
        if (sceneCamera_ && orbitController_) {
            DebugCameraCVars::RestoreTo(*sceneCamera_, *orbitController_);
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

        // 保存が無ければ何もしない。エディタ視点は CVar 側の値がそのまま残る。
        CameraSceneStateIO::Load(ctx.saveSystem->GetSceneName(), *cameraManager_);
    }

    void CameraFeature::Update(SceneContext& ctx, SceneUpdatePhase phase)
    {
        if (phase != SceneUpdatePhase::FrameStart || !cameraManager_) {
            return;
        }

        // 入力の正規化（ImGui / InputManager 依存）は EditorCameraInput に閉じており、
        // コントローラは CameraInputState しか見ない。
        // カメラ操作はポーズやスローの影響を受けない
        cameraManager_->Update(EditorCameraInput::Collect(ctx.engine), Time::UnscaledDeltaTime());

        // 更新後の設定・姿勢を CVar へ写す（カメラ UI・マウス操作のどちらの変更も拾う）
        MirrorEditorCameraToCVars();
    }

    void CameraFeature::Finalize(SceneContext&)
    {
        // 最後の設定・姿勢を CVar へ写しておく（最終フレームの Update 以降の変更を取りこぼさない）。
        // カメラの破棄より先に行うこと
        MirrorEditorCameraToCVars();

        sceneCamera_ = nullptr;
        orbitController_ = nullptr;
    }

    void CameraFeature::MirrorEditorCameraToCVars()
    {
        if (sceneCamera_ && orbitController_) {
            DebugCameraCVars::MirrorFrom(*sceneCamera_, *orbitController_);
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
