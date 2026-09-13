#pragma once

#include "EngineSystem/EngineSystem.h"
#include "GameObject/GameObject.h"
#include "Scene/SceneManager.h"

namespace CoreEngine
{
    class Camera;
}

namespace GameComponents
{
    /// @brief 今のシーンのゲーム視点カメラを返す
    /// @details エディタ視点で覗いている間もゲーム側のカメラを返す。
    ///          カメラはシーンの持ち物なので、ポインタを保持せず使うたびに引く。
    /// @param owner 呼び出し元コンポーネントのオーナー
    /// @return シーンやカメラがまだ無ければ nullptr
    inline CoreEngine::Camera* FindGameCamera(const CoreEngine::GameObject* owner)
    {
        CoreEngine::EngineSystem* engine = owner ? owner->GetEngineSystem() : nullptr;
        CoreEngine::SceneManager* sceneManager = engine ? engine->GetSceneManager() : nullptr;
        return sceneManager ? sceneManager->GetGameCamera3D() : nullptr;
    }
}
