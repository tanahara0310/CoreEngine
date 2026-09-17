#include "pch.h"
#include "Editor/Scene/PlayModeController.h"

#ifdef CORE_EDITOR

#include "Audio/AudioSystem.h"
#include "Camera/CameraManager.h"
#include "Editor/Command/EditorCommandStack.h"
#include "Editor/Scene/SceneDebugEditor.h"
#include "EngineSystem/EngineSystem.h"
#include "EngineSystem/PlaybackState.h"
#include "GameObject/GameObjectManager.h"
#include "GameObject/ObjectId.h"
#include "Scene/SceneManager.h"
#include "Scene/SceneSaveSystem.h"
#include "Utility/Debug/GameDebugUI.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/Logger/Logger.h"

#include <chrono>
#include <optional>
#include <utility>

namespace CoreEngine::Editor
{
    namespace
    {
        /// @brief 始めてからの秒数
        double SecondsSince(std::chrono::steady_clock::time_point start)
        {
            return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        }

        /// @brief エディタの視点に使うカメラだけを含む状態を作る（役割の名前は持たない）
        CameraSceneState ExtractEditorView(const CameraSceneState& state)
        {
            CameraSceneState view;
            for (const CameraSceneStateEntry& entry : state.cameras) {
                if (entry.name == state.sceneCameraName) {
                    view.cameras.push_back(entry);
                }
            }
            return view;
        }
    }

    PlayModeController::~PlayModeController()
    {
        Finalize();
    }

    void PlayModeController::Initialize(EngineSystem* engine, GameDebugUI* gameDebugUI)
    {
        engine_ = engine;
        gameDebugUI_ = gameDebugUI;

        PlaybackStateManager::GetInstance().SetTransitionHooks({
            .beforePlay = [this] { return CapturePlayStart(); },
            .afterStop = [this] { return RestorePlayStart(); },
            });
    }

    void PlayModeController::Finalize()
    {
        if (!engine_) {
            return;
        }

        PlaybackStateManager::GetInstance().ClearTransitionHooks();
        snapshot_.reset();
        engine_ = nullptr;
        gameDebugUI_ = nullptr;
    }

    std::size_t PlayModeController::GetSnapshotObjectCount() const
    {
        return snapshot_ ? snapshot_->objects.size() : 0;
    }

    float PlayModeController::GetPlayTime() const
    {
        if (!PlaybackStateManager::GetInstance().IsInPlayMode()) {
            return 0.0f;
        }
        return Time::TimeSinceStartup() - playStartTime_;
    }

    bool PlayModeController::CapturePlayStart()
    {
        SceneManager* const sceneManager = engine_ ? engine_->GetSceneManager() : nullptr;
        if (sceneManager && !sceneManager->CanLoadSceneNow()) {
            return false;
        }

        snapshot_.reset();
        captureSeconds_ = 0.0;
        timeScale_ = Time::TimeScale();
        playStartTime_ = Time::TimeSinceStartup();

        GameObjectManager* const objects = sceneManager ? sceneManager->GetCurrentGameObjectManager() : nullptr;
        const std::string sceneName = sceneManager ? sceneManager->GetCurrentSceneName() : std::string{};
        if (!objects || !sceneManager->HasScene(sceneName)) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                "PlayModeController: シーンを控えられないので、停止しても再生前の状態へ戻せません");
            return true;
        }

        const auto started = std::chrono::steady_clock::now();
        snapshot_ = SceneSaveSystem::CaptureSnapshot(*objects);
        sceneName_ = sceneName;

        SceneDebugEditor* const editor = gameDebugUI_ ? gameDebugUI_->GetSceneDebugEditor() : nullptr;
        const CameraManager* const cameras = editor ? editor->GetCameraManager() : nullptr;
        hasCameraState_ = cameras != nullptr;
        if (cameras) {
            cameraState_ = CameraSceneStateIO::Capture(*cameras);
        }
        sceneDirty_ = editor && editor->IsSceneDirty();
        captureSeconds_ = SecondsSince(started);

        // 再生中の操作は別の履歴に積み、停止したら再生前の履歴へ戻す
        EditorCommandStack::Get().BeginPlaySession();

        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System,
            "PlayModeController: 再生を始めます。シーン \"{}\" のオブジェクト {} 体を控えました（{:.3f} 秒）",
            sceneName_, snapshot_->objects.size(), captureSeconds_);
        return true;
    }

    bool PlayModeController::RestorePlayStart()
    {
        if (!snapshot_) {
            ResetPlaySession();
            return true;
        }

        SceneManager* const sceneManager = engine_ ? engine_->GetSceneManager() : nullptr;
        if (!sceneManager) {
            snapshot_.reset();
            ResetPlaySession();
            return true;
        }
        if (!sceneManager->CanLoadSceneNow()) {
            return false;
        }

        ResetPlaySession();

        // 組み直した後も残すエディタの視点と選択を控える
        SceneDebugEditor* editor = gameDebugUI_ ? gameDebugUI_->GetSceneDebugEditor() : nullptr;
        CameraManager* cameras = editor ? editor->GetCameraManager() : nullptr;
        std::optional<CameraSceneState> editorView;
        bool useSceneCamera = false;
        if (cameras) {
            editorView = ExtractEditorView(CameraSceneStateIO::Capture(*cameras));
            useSceneCamera = cameras->IsUsingSceneCamera();
        }
        ObjectId selectedId{};
        if (const GameObject* const selected = editor ? editor->GetSelectedObject() : nullptr) {
            selectedId = selected->GetObjectId();
        }

        // 再生前の履歴を取り出してから組み直し、組み直した後に戻す
        EditorCommandStack::History history = EditorCommandStack::Get().EndPlaySession();

        const auto started = std::chrono::steady_clock::now();
        const std::shared_ptr<const SceneSnapshot> snapshot = std::move(snapshot_);
        const bool loaded = sceneManager->LoadSceneFromSnapshot(sceneName_, snapshot);
        EditorCommandStack::Get().RestoreHistory(std::move(history));
        if (!loaded) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::System,
                "PlayModeController: 再生前のシーン \"{}\" を組み直せませんでした", sceneName_);
            return true;
        }
        const double restoreSeconds = SecondsSince(started);

        editor = gameDebugUI_ ? gameDebugUI_->GetSceneDebugEditor() : nullptr;
        cameras = editor ? editor->GetCameraManager() : nullptr;
        if (cameras) {
            if (hasCameraState_) {
                CameraSceneStateIO::Apply(cameraState_, *cameras);
            }
            if (editorView) {
                CameraSceneStateIO::Apply(*editorView, *cameras);
                cameras->SetUseSceneCamera(useSceneCamera);
            }
        }
        if (editor) {
            if (sceneDirty_) {
                editor->MarkSceneDirty();
            }
            GameObjectManager* const objects = sceneManager->GetCurrentGameObjectManager();
            if (GameObject* const reselected = (objects && selectedId.IsValid()) ? objects->FindObject(selectedId) : nullptr) {
                editor->SelectObject(reselected);
            }
        }

        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System,
            "PlayModeController: 再生をやめ、シーン \"{}\" を再生前の状態へ戻しました（オブジェクト {} 体・{:.3f} 秒）",
            sceneName_, snapshot->objects.size(), restoreSeconds);
        return true;
    }

    void PlayModeController::ResetPlaySession() const
    {
        if (AudioSystem* const audio = engine_ ? engine_->GetService<AudioSystem>() : nullptr) {
            audio->StopAll();
        }
        Time::SetTimeScale(timeScale_);
    }
}

#endif // CORE_EDITOR
