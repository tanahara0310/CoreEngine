#include "pch.h"
#include "Editor/Scene/LastOpenedScene.h"

#ifdef CORE_EDITOR

#include "Utility/JsonManager/JsonManager.h"

#include <algorithm>

namespace CoreEngine::Editor::LastOpenedScene
{
    namespace
    {
        constexpr const char* kFilePath = "Application/Saved/EditorSettings/LastScene.json";
        constexpr const char* kSceneKey = "scene";
        constexpr const char* kRecentKey = "recent";
        constexpr std::size_t kMaxRecent = 10;

        json LoadFile()
        {
            JsonManager& jm = JsonManager::GetInstance();
            if (!jm.FileExists(kFilePath)) {
                return json::object();
            }
            json data = jm.LoadJson(kFilePath);
            return data.is_object() ? data : json::object();
        }

        /// @brief 最近開いた順の名前（新しい順・空の名前と重なりは除く）
        std::vector<std::string> ReadRecent(const json& data)
        {
            std::vector<std::string> recent;
            for (std::string& name : JsonManager::SafeGet(data, kRecentKey, std::vector<std::string>())) {
                if (!name.empty() && std::find(recent.begin(), recent.end(), name) == recent.end()) {
                    recent.push_back(std::move(name));
                }
            }
            if (recent.empty()) {
                if (std::string last = JsonManager::SafeGet(data, kSceneKey, std::string()); !last.empty()) {
                    recent.push_back(std::move(last));
                }
            }
            if (recent.size() > kMaxRecent) {
                recent.resize(kMaxRecent);
            }
            return recent;
        }
    }

    std::string Load()
    {
        return JsonManager::SafeGet(LoadFile(), kSceneKey, std::string());
    }

    std::vector<std::string> LoadRecent()
    {
        return ReadRecent(LoadFile());
    }

    void Save(const std::string& sceneName)
    {
        if (sceneName.empty()) {
            return;
        }
        const json data = LoadFile();
        std::vector<std::string> recent = ReadRecent(data);
        if (JsonManager::SafeGet(data, kSceneKey, std::string()) == sceneName
            && data.contains(kRecentKey) && !recent.empty() && recent.front() == sceneName) {
            return;
        }

        recent.erase(std::remove(recent.begin(), recent.end(), sceneName), recent.end());
        recent.insert(recent.begin(), sceneName);
        if (recent.size() > kMaxRecent) {
            recent.resize(kMaxRecent);
        }
        JsonManager::GetInstance().SaveJson(kFilePath, json{ { kSceneKey, sceneName }, { kRecentKey, recent } });
    }
}

#endif // CORE_EDITOR
