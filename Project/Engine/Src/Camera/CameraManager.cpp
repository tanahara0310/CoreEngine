#include "pch.h"
#include "CameraManager.h"

#include "Utility/Logger/Logger.h"

#ifdef CORE_EDITOR
#include "Editor/Camera/CameraDebugUI.h"
#include "GameObject/GameObjectManager.h"
#endif

namespace CoreEngine
{

    CameraManager::CameraManager() = default;

    CameraManager::~CameraManager() = default;

    bool CameraManager::RegisterCamera(const std::string& name, std::unique_ptr<Camera> camera)
    {
        if (!camera) {
            return false;
        }

        // 登録済みの名前なら差し替えずに断る
        if (cameras_.find(name) != cameras_.end()) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                "カメラ {} は登録済みなので、新しいカメラを登録しない", name);
            return false;
        }

        cameras_.emplace(name, std::move(camera));
        return true;
    }

    void CameraManager::UnregisterCamera(const std::string& name)
    {
        auto it = cameras_.find(name);
        if (it == cameras_.end()) {
            return;
        }

        controllers_.erase(name);
        objectOwnedCameras_.erase(name);
        displayNames_.erase(name);
        cameras_.erase(it);
    }

    void CameraManager::SetObjectOwnedCamera(const std::string& name, bool owned)
    {
        if (owned) {
            objectOwnedCameras_.insert(name);
        } else {
            objectOwnedCameras_.erase(name);
        }
    }

    bool CameraManager::IsObjectOwnedCamera(const std::string& name) const
    {
        return objectOwnedCameras_.find(name) != objectOwnedCameras_.end();
    }

    void CameraManager::SetDisplayName(const std::string& name, const std::string& displayName)
    {
        if (cameras_.find(name) == cameras_.end()) {
            return;
        }
        displayNames_[name] = displayName;
    }

    std::string CameraManager::GetDisplayName(const std::string& name) const
    {
        auto it = displayNames_.find(name);
        return (it != displayNames_.end()) ? it->second : name;
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

    Camera* CameraManager::GetCamera(const std::string& name) const
    {
        auto it = cameras_.find(name);
        return (it != cameras_.end()) ? it->second.get() : nullptr;
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

#ifdef CORE_EDITOR
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
    void CameraManager::DrawDebugViewportOverlay(const Camera& viewCamera,
        const CameraEditorViewport& viewport)
    {
        if (debugUI_) {
            debugUI_->DrawViewportOverlay(viewCamera, viewport);
        }
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
