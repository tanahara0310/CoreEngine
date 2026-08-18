#include "pch.h"
#include "SceneSaveSystem.h"
#include "GameObject/GameObjectManager.h"
#include "GameObject/Model/DynamicModelObject.h"
#include "Utility/JsonManager/JsonManager.h"

#include <algorithm>

namespace CoreEngine
{
    // ===== パスヘルパー =====

    std::string SceneSaveSystem::MakeSceneDir(const std::string& sceneName) {
        return "Application/Assets/Scenes/" + sceneName;
    }

    std::string SceneSaveSystem::MakeManifestPath(const std::string& sceneName) {
        return MakeSceneDir(sceneName) + "/_scene.json";
    }

    std::string SceneSaveSystem::MakeObjectPath(const std::string& sceneName, const std::string& key) {
        return MakeSceneDir(sceneName) + "/" + key + ".json";
    }

    std::string SceneSaveSystem::GetSceneDir() const {
        return MakeSceneDir(sceneName_);
    }

    std::string SceneSaveSystem::GetManifestPath() const {
        return MakeManifestPath(sceneName_);
    }

    std::string SceneSaveSystem::GetObjectPath(const std::string& key) const {
        return MakeObjectPath(sceneName_, key);
    }

    // ===== 先読み用のパス列挙 =====

    std::vector<std::string> SceneSaveSystem::CollectModelPaths(const std::string& sceneName)
    {
        std::vector<std::string> modelPaths;
        if (sceneName.empty()) {
            return modelPaths;
        }

        auto& jm = JsonManager::GetInstance();

        const std::string manifestPath = MakeManifestPath(sceneName);
        if (!jm.FileExists(manifestPath)) {
            return modelPaths;
        }

        json manifest = jm.LoadJson(manifestPath);
        if (!manifest.contains("objects") || !manifest["objects"].is_array()) {
            return modelPaths;
        }

        for (const auto& entry : manifest["objects"]) {
            if (!entry.is_string()) continue;

            const std::string key = entry.get<std::string>();
            if (key.empty()) continue;

            const std::string objPath = MakeObjectPath(sceneName, key);
            if (!jm.FileExists(objPath)) continue;

            json data = jm.LoadJson(objPath);
            if (data.is_null()) continue;

            if (data.contains("modelPath") && data["modelPath"].is_string()) {
                std::string modelPath = data["modelPath"].get<std::string>();
                if (modelPath.empty()) continue;

                // 同じモデルを複数オブジェクトが共有するのが普通なので重複を潰す。
                // ここで潰さなくても ModelManager 側のロード権で 1 回に収束するが、
                // 無駄なタスクをスレッドプールへ積まない
                if (std::find(modelPaths.begin(), modelPaths.end(), modelPath) == modelPaths.end()) {
                    modelPaths.push_back(std::move(modelPath));
                }
            }
        }

        return modelPaths;
    }

    // ===== Load =====

    void SceneSaveSystem::Load(GameObjectManager* mgr)
    {
        if (sceneName_.empty() || !mgr) return;

        auto& jm = JsonManager::GetInstance();

        auto findObjectBySerializeKey = [mgr](const std::string& key) -> GameObject* {
            for (const auto& obj : mgr->GetAllObjects()) {
                if (obj && obj->GetSerializeKey() == key) {
                    return obj.get();
                }
            }
            return nullptr;
        };

        const std::string manifestPath = GetManifestPath();
        if (jm.FileExists(manifestPath)) {
            json manifest = jm.LoadJson(manifestPath);
            if (manifest.contains("objects") && manifest["objects"].is_array()) {
                for (const auto& entry : manifest["objects"]) {
                    if (!entry.is_string()) continue;

                    const std::string key = entry.get<std::string>();
                    if (key.empty() || findObjectBySerializeKey(key)) {
                        continue;
                    }

                    const std::string objPath = GetObjectPath(key);
                    if (!jm.FileExists(objPath)) {
                        continue;
                    }

                    json data = jm.LoadJson(objPath);
                    if (data.is_null()) {
                        continue;
                    }

                    if (data.contains("modelPath") && data["modelPath"].is_string()) {
                        auto obj = std::make_unique<DynamicModelObject>();
                        obj->SetModelPath(data["modelPath"].get<std::string>());
                        obj->SetName(key);
                        mgr->AddObject(std::move(obj));
                    }
                }
            }
        }

        // オブジェクトごとに個別ファイルからデシリアライズ
        for (const auto& obj : mgr->GetAllObjects()) {
            if (!obj || !obj->IsSerializeEnabled()) continue;
            const std::string& key = obj->GetSerializeKey();
            if (key.empty()) continue;

            std::string objPath = GetObjectPath(key);
            if (!jm.FileExists(objPath)) continue;

            json data = jm.LoadJson(objPath);
            if (!data.is_null()) {
                obj->OnDeserialize(data);
            }
        }
    }

    // ===== SaveScene =====

    void SceneSaveSystem::SaveScene(GameObjectManager* mgr)
    {
        if (sceneName_.empty() || !mgr) return;

        auto& jm = JsonManager::GetInstance();
        jm.CreateJsonDirectory(GetSceneDir());

        // マニフェスト（オブジェクトキー一覧）
        json manifest;
        manifest["objects"] = json::array();

        // 各オブジェクトを個別ファイルに保存
        for (const auto& obj : mgr->GetAllObjects()) {
            if (!obj || !obj->IsSerializeEnabled()) continue;
            const std::string& key = obj->GetSerializeKey();
            if (key.empty()) continue;

            json data = obj->OnSerialize();
            if (!data.empty()) {
                jm.SaveJson(GetObjectPath(key), data);
                manifest["objects"].push_back(key);
            }
        }

        // マニフェストを保存
        jm.SaveJson(GetManifestPath(), manifest);

        if (onSaveNotification_) {
            onSaveNotification_("シーンを保存しました: " + sceneName_);
        }
    }

    // ===== SaveObject =====

    void SceneSaveSystem::SaveObject(GameObject* obj)
    {
        if (sceneName_.empty() || !obj || !obj->IsSerializeEnabled()) return;
        const std::string& key = obj->GetSerializeKey();
        if (key.empty()) return;

        auto& jm = JsonManager::GetInstance();
        jm.CreateJsonDirectory(GetSceneDir());

        // オブジェクトデータを個別ファイルに保存
        json data = obj->OnSerialize();
        if (!data.empty()) {
            jm.SaveJson(GetObjectPath(key), data);
        }

        // マニフェストにキーが含まれていなければ追加
        std::string manifestPath = GetManifestPath();
        json manifest;
        if (jm.FileExists(manifestPath)) {
            manifest = jm.LoadJson(manifestPath);
        }
        if (!manifest.contains("objects") || !manifest["objects"].is_array()) {
            manifest["objects"] = json::array();
        }

        bool found = false;
        for (const auto& k : manifest["objects"]) {
            if (k.is_string() && k.get<std::string>() == key) {
                found = true;
                break;
            }
        }
        if (!found) {
            manifest["objects"].push_back(key);
            jm.SaveJson(manifestPath, manifest);
        }

        if (onSaveNotification_) {
            onSaveNotification_("\"" + obj->GetName() + "\" を保存しました");
        }
    }
}
