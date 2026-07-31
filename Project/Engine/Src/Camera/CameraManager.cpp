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
        if (cameras_.find(name) != cameras_.end()) {
            if (cameraType == CameraType::Camera3D && activeCamera3DName_ == name) {
                activeCamera3DName_.clear();
                activeCamera3D_ = nullptr;
            } else if (cameraType == CameraType::Camera2D && activeCamera2DName_ == name) {
                activeCamera2DName_.clear();
                activeCamera2D_ = nullptr;
            }
        }

        cameras_[name] = std::move(camera);

        // タイプごとに最初に登録されたカメラを自動的にアクティブに設定
        if (cameraType == CameraType::Camera3D && !activeCamera3D_) {
            SetActiveCamera(name, CameraType::Camera3D);
        } else if (cameraType == CameraType::Camera2D && !activeCamera2D_) {
            SetActiveCamera(name, CameraType::Camera2D);
        }
    }

    void CameraManager::UnregisterCamera(const std::string& name)
    {
        auto it = cameras_.find(name);
        if (it == cameras_.end()) {
            return;
        }

        const CameraType cameraType = it->second->GetCameraType();

        if (cameraType == CameraType::Camera3D && activeCamera3DName_ == name) {
            activeCamera3DName_.clear();
            activeCamera3D_ = nullptr;
        } else if (cameraType == CameraType::Camera2D && activeCamera2DName_ == name) {
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
        return activeCamera3DName_.empty() ? nullptr : GetController(activeCamera3DName_);
    }

    bool CameraManager::SetActiveCamera(const std::string& name, CameraType type)
    {
        auto it = cameras_.find(name);
        if (it == cameras_.end()) {
            return false;
        }

        if (it->second->GetCameraType() != type) {
            return false;
        }

        if (type == CameraType::Camera3D) {
            activeCamera3DName_ = name;
            activeCamera3D_ = it->second.get();
        } else if (type == CameraType::Camera2D) {
            activeCamera2DName_ = name;
            activeCamera2D_ = it->second.get();
        }

        return true;
    }

    Camera* CameraManager::GetActiveCamera(CameraType type) const
    {
        if (type == CameraType::Camera3D) {
            return activeCamera3D_;
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
            return activeCamera3DName_;
        }
        if (type == CameraType::Camera2D) {
            return activeCamera2DName_;
        }
        return empty;
    }

    void CameraManager::Update(const CameraInputState& input, float deltaTime)
    {
        for (auto& [name, camera] : cameras_) {
            if (!camera || !camera->GetActive()) {
                continue;
            }

            // コントローラ（付いていれば）が先に Transform を書き、そのあと行列を作る。
            // この順序が「操作 → 姿勢 → 行列」の一方向の流れを保証する。
            //
            // 入力は非アクティブなカメラにも同じものが渡るが、実際に操作されるのは
            // input.active（ビューポート上で操作中）のときだけ。
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
