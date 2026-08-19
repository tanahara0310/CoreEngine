#include "pch.h"
#include "CameraManager.h"

#ifdef USE_IMGUI
#include "Editor/Camera/CameraDebugUI.h"
#include "GameObject/GameObjectManager.h"
#endif

namespace CoreEngine
{

    CameraManager::CameraManager() = default;

    CameraManager::~CameraManager() = default;

    void CameraManager::RegisterCamera(const std::string& name, std::unique_ptr<Camera> camera)
    {
        if (!camera) {
            return;
        }

        const CameraType cameraType = camera->GetCameraType();

        // 既存の同名カメラがあればアクティブ参照をクリアしてから差し替える
        if (cameras_.find(name) != cameras_.end()
            && cameraType == CameraType::Camera2D && activeCamera2DName_ == name) {
            activeCamera2DName_.clear();
            activeCamera2D_ = nullptr;
        }

        cameras_[name] = std::move(camera);

        // 2D は最初に登録されたものを自動的にアクティブにする。
        // 3D は役割（Scene / Game）で引くため、ここでアクティブ指定は行わない。
        if (cameraType == CameraType::Camera2D && !activeCamera2D_) {
            SetActiveCamera(name, CameraType::Camera2D);
        }
    }

    void CameraManager::UnregisterCamera(const std::string& name)
    {
        auto it = cameras_.find(name);
        if (it == cameras_.end()) {
            return;
        }

        if (it->second->GetCameraType() == CameraType::Camera2D && activeCamera2DName_ == name) {
            activeCamera2DName_.clear();
            activeCamera2D_ = nullptr;
        }

        controllers_.erase(name);
        cameras_.erase(it);
    }

    ICameraController* CameraManager::GetController(const std::string& name) const
    {
        auto it = controllers_.find(name);
        return (it != controllers_.end()) ? it->second.get() : nullptr;
    }

    ICameraController* CameraManager::GetActiveController() const
    {
        const std::string& name = GetViewCameraName();
        return name.empty() ? nullptr : GetController(name);
    }

    const std::string& CameraManager::GetViewCameraName() const
    {
        static const std::string empty;

        // 覗いている側が未登録なら、もう一方へフォールバックする
        // （シーンが片方しか用意していない場合でも画が出るように）
        const std::string& primary = useSceneCamera_ ? sceneCameraName_ : gameCameraName_;
        if (cameras_.find(primary) != cameras_.end()) {
            return primary;
        }

        const std::string& fallback = useSceneCamera_ ? gameCameraName_ : sceneCameraName_;
        if (cameras_.find(fallback) != cameras_.end()) {
            return fallback;
        }

        return empty;
    }

    // 描画・ギズモ・ピッキングが見るカメラ。Scene / Game のどちらを覗いているかで決まる
    Camera* CameraManager::GetViewCamera() const
    {
        const std::string& name = GetViewCameraName();
        return name.empty() ? nullptr : GetCamera(name);
    }

    bool CameraManager::SetActiveCamera(const std::string& name, CameraType type)
    {
        // 3D は役割（Scene / Game）で決まるため、ここでは 2D のみ扱う
        if (type != CameraType::Camera2D) {
            return false;
        }

        auto it = cameras_.find(name);
        if (it == cameras_.end() || it->second->GetCameraType() != CameraType::Camera2D) {
            return false;
        }

        activeCamera2DName_ = name;
        activeCamera2D_ = it->second.get();
        return true;
    }

    Camera* CameraManager::GetActiveCamera(CameraType type) const
    {
        // 3D は「今覗いているカメラ」と同一。編集対象と描画対象が食い違わないようにする。
        if (type == CameraType::Camera3D) {
            return GetViewCamera();
        }
        if (type == CameraType::Camera2D) {
            return activeCamera2D_;
        }
        return nullptr;
    }

    Camera* CameraManager::GetCamera(const std::string& name) const
    {
        auto it = cameras_.find(name);
        return (it != cameras_.end()) ? it->second.get() : nullptr;
    }

    const std::string& CameraManager::GetActiveCameraName(CameraType type) const
    {
        static const std::string empty;
        if (type == CameraType::Camera3D) {
            return GetViewCameraName();
        }
        if (type == CameraType::Camera2D) {
            return activeCamera2DName_;
        }
        return empty;
    }

    // コントローラ → Transform → 行列の順に一方向で流す。
    // 入力は全カメラへ渡るが、実際に動くのは操作中（input.active）のものだけ
    void CameraManager::Update(const CameraInputState& input, float deltaTime)
    {
        for (auto& [name, camera] : cameras_) {
            if (!camera || !camera->GetActive()) {
                continue;
            }

            // コントローラ（付いていれば）が先に Transform を書き、そのあと行列を作る。
            // この順序が「操作 → 姿勢 → 行列」の一方向の流れを保証する。
            // 入力は非アクティブなカメラにも渡るが、実際に操作されるのは input.active のときだけ。
            if (auto it = controllers_.find(name); it != controllers_.end() && it->second) {
                it->second->Update(input, deltaTime, *camera);
            }

            camera->Update();
        }
    }

#ifdef USE_IMGUI
    void CameraManager::SetDebugGameObjectManager(GameObjectManager* gameObjectManager)
    {
        debugGameObjectManager_ = gameObjectManager;
        if (debugUI_) {
            debugUI_->SetGameObjectManager(debugGameObjectManager_);
        }
    }

    void CameraManager::DrawImGui()
    {
        if (!debugUI_) {
            debugUI_ = std::make_unique<CameraDebugUI>();
            debugUI_->Initialize(this);
        }
        debugUI_->SetGameObjectManager(debugGameObjectManager_);
        debugUI_->SetEngineSystem(engineSystem_);
        debugUI_->Draw();
    }

    // デバッグ用モジュール（追従・シーケンス再生）を毎フレーム回す
    void CameraManager::UpdateDebugModules()
    {
        if (!debugUI_) {
            debugUI_ = std::make_unique<CameraDebugUI>();
            debugUI_->Initialize(this);
        }
        debugUI_->SetGameObjectManager(debugGameObjectManager_);
        debugUI_->SetEngineSystem(engineSystem_);
        debugUI_->UpdateModules();
    }

    void CameraManager::DrawImGuiContent()
    {
        if (!debugUI_) {
            debugUI_ = std::make_unique<CameraDebugUI>();
            debugUI_->Initialize(this);
        }
        debugUI_->SetGameObjectManager(debugGameObjectManager_);
        debugUI_->SetEngineSystem(engineSystem_);
        debugUI_->DrawContent();
    }
#endif
}
