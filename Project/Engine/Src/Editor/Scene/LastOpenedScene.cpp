#include "pch.h"
#include "Editor/Scene/LastOpenedScene.h"

#ifdef CORE_EDITOR

#include "Utility/JsonManager/JsonManager.h"

namespace CoreEngine::Editor::LastOpenedScene
{
    namespace
    {
        constexpr const char* kFilePath = "Application/Saved/EditorSettings/LastScene.json";
        constexpr const char* kSceneKey = "scene";
    }

    std::string Load()
    {
        JsonManager& jm = JsonManager::GetInstance();
        if (!jm.FileExists(kFilePath)) {
            return {};
        }
        return JsonManager::SafeGet(jm.LoadJson(kFilePath), kSceneKey, std::string());
    }

    void Save(const std::string& sceneName)
    {
        if (sceneName.empty() || Load() == sceneName) {
            return;
        }
        JsonManager::GetInstance().SaveJson(kFilePath, json{ { kSceneKey, sceneName } });
    }
}

#endif // CORE_EDITOR
