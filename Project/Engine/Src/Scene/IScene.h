#pragma once
#include <string>
#include <functional>

// 前方宣言
namespace CoreEngine {
    class EngineSystem;
    class SceneManager;
}

/// @brief シーンインターフェース

namespace CoreEngine
{
class IScene {
public:
    virtual ~IScene() = default;

    virtual void Initialize(CoreEngine::EngineSystem* engine) = 0;
    virtual void Update() = 0;
    virtual void Draw() = 0;
    virtual void Finalize() = 0;

    /// @brief SceneManager への参照を設定（自動呼び出し）
    virtual void SetSceneManager(CoreEngine::SceneManager* sceneManager) {
        sceneManager_ = sceneManager;
    }

protected:
    SceneManager* sceneManager_ = nullptr;
};
}
