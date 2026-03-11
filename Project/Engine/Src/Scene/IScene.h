#pragma once
#include <string>
#include <functional>

// 前方宣言
namespace CoreEngine {
    class EngineSystem;
    class SceneManager;
    class ICamera;
    class GameObjectManager;
}

/// @brief シーンインターフェース

namespace CoreEngine
{
class IScene {
public:
    virtual ~IScene() = default;

    virtual void Initialize(CoreEngine::EngineSystem* engine) = 0;
    virtual void Update() = 0;
    virtual void PrepareRender() {}
    virtual void Draw() = 0;
    virtual void DrawSceneView() { Draw(); }
    virtual void Finalize() = 0;

    virtual ICamera* GetSceneViewCamera() const { return nullptr; }
    virtual ICamera* GetGameViewCamera2D() const { return nullptr; }
    virtual GameObjectManager* GetGameObjectManager() { return nullptr; }

    /// @brief SceneManager への参照を設定（自動呼び出し）
    virtual void SetSceneManager(CoreEngine::SceneManager* sceneManager) {
        sceneManager_ = sceneManager;
    }

protected:
    SceneManager* sceneManager_ = nullptr;
};
}
