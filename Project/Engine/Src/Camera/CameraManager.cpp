#include "CameraManager.h"

#ifdef _DEBUG
#include "Camera/Debug/CameraDebugUI.h"
#endif

namespace CoreEngine
{


    CameraManager::CameraManager() = default;

    CameraManager::~CameraManager() = default;

    void CameraManager::RegisterCamera(const std::string& name, std::unique_ptr<ICamera> camera)
    {
        if (!camera) {
            return;
        }

        CameraType cameraType = camera->GetCameraType();

        // 既存の同名カメラがあれば削除
        if (cameras_.find(name) != cameras_.end()) {
            // アクティブカメラだった場合はクリア
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

        CameraType cameraType = it->second->GetCameraType();

        // アクティブカメラだった場合はクリア
        if (cameraType == CameraType::Camera3D && activeCamera3DName_ == name) {
            activeCamera3DName_.clear();
            activeCamera3D_ = nullptr;
        } else if (cameraType == CameraType::Camera2D && activeCamera2DName_ == name) {
            activeCamera2DName_.clear();
            activeCamera2D_ = nullptr;
        }

        cameras_.erase(it);
    }

    bool CameraManager::SetActiveCamera(const std::string& name, CameraType type)
    {
        auto it = cameras_.find(name);
        if (it == cameras_.end()) {
            return false;
        }

        // カメラタイプが一致するか確認
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

    ICamera* CameraManager::GetActiveCamera(CameraType type) const
    {
        if (type == CameraType::Camera3D) {
            return activeCamera3D_;
        } else if (type == CameraType::Camera2D) {
            return activeCamera2D_;
        }
        return nullptr;
    }

    ICamera* CameraManager::GetCamera(const std::string& name) const
    {
        auto it = cameras_.find(name);
        if (it == cameras_.end()) {
            return nullptr;
        }
        return it->second.get();
    }

    const Matrix4x4& CameraManager::GetViewMatrix() const
    {
        static Matrix4x4 identity = MathCore::Matrix::Identity();
        if (!activeCamera3D_) {
            return identity;
        }
        return activeCamera3D_->GetViewMatrix();
    }

    const Matrix4x4& CameraManager::GetProjectionMatrix() const
    {
        static Matrix4x4 identity = MathCore::Matrix::Identity();
        if (!activeCamera3D_) {
            return identity;
        }
        return activeCamera3D_->GetProjectionMatrix();
    }

    Vector3 CameraManager::GetCameraPosition() const
    {
        if (!activeCamera3D_) {
            return { 0.0f, 0.0f, 0.0f };
        }
        return activeCamera3D_->GetPosition();
    }

    const std::string& CameraManager::GetActiveCameraName(CameraType type) const
    {
        static std::string empty = "";
        if (type == CameraType::Camera3D) {
            return activeCamera3DName_;
        } else if (type == CameraType::Camera2D) {
            return activeCamera2DName_;
        }
        return empty;
    }

    void CameraManager::Update()
    {
        for (auto& [name, camera] : cameras_) {
            (void)name;
            if (camera && camera->GetActive()) {
                camera->Update();
            }
        }
    }

#ifdef _DEBUG
    void CameraManager::DrawImGui()
    {
        // デバッグUIを遅延初期化
        if (!debugUI_) {
            debugUI_ = std::make_unique<CameraDebugUI>();
            debugUI_->Initialize(this);
        }

        // デバッグUIに描画を委譲
        debugUI_->Draw();
    }
#endif
}
