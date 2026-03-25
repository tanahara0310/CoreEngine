#include "SceneSaveSystem.h"
#include "ObjectCommon/GameObjectManager.h"
#include "ObjectCommon/Sprite/SpriteObject.h"
#include "ObjectCommon/Model/ModelGameObject.h"
#include "Scene/SceneSerializer.h"
#include "Utility/JsonManager/JsonManager.h"

namespace CoreEngine
{
    void SceneSaveSystem::Load(GameObjectManager* mgr)
    {
        if (sceneName_.empty() || !mgr) return;

        std::string filePath = "Application/Assets/Scene/" + sceneName_ + ".json";
        auto& jsonManager = JsonManager::GetInstance();

        if (!jsonManager.FileExists(filePath)) return;

        json j = jsonManager.LoadJson(filePath);
        if (!j.contains("objects")) return;

        const json& objects = j["objects"];
        for (const auto& obj : mgr->GetAllObjects()) {
            if (!obj || !obj->IsSerializeEnabled()) continue;
            const std::string& name = obj->GetName();
            if (name.empty()) continue;
            if (objects.contains(name)) {
                auto* spriteObj = dynamic_cast<SpriteObject*>(obj.get());
                if (spriteObj) {
                    SceneSerializer::LoadSpriteObject(spriteObj, objects[name]);
                } else if (auto* modelObj = dynamic_cast<ModelGameObject*>(obj.get())) {
                    SceneSerializer::LoadObject(modelObj, objects[name]);
                }
            }
        }
    }

    void SceneSaveSystem::Save(GameObjectManager* mgr)
    {
        if (sceneName_.empty() || !mgr) return;

        std::string dirPath = "Application/Assets/Scene";
        std::string filePath = dirPath + "/" + sceneName_ + ".json";
        auto& jsonManager = JsonManager::GetInstance();

        jsonManager.CreateJsonDirectory(dirPath);

        json j;
        for (const auto& obj : mgr->GetAllObjects()) {
            if (!obj || !obj->IsSerializeEnabled()) continue;
            const std::string& name = obj->GetName();
            if (name.empty()) continue;
            auto* spriteObj = dynamic_cast<SpriteObject*>(obj.get());
            if (spriteObj) {
                SceneSerializer::SaveSpriteObject(spriteObj, j["objects"][name]);
            } else if (auto* modelObj = dynamic_cast<ModelGameObject*>(obj.get())) {
                SceneSerializer::SaveObject(modelObj, j["objects"][name]);
            }
        }

        jsonManager.SaveJson(filePath, j);

        if (onSaveNotification_) {
            onSaveNotification_("シーン全体を保存しました: " + sceneName_ + ".json");
        }
    }

    void SceneSaveSystem::SaveSingle(GameObject* obj)
    {
        if (sceneName_.empty() || !obj || !obj->IsSerializeEnabled()) return;
        const std::string& name = obj->GetName();
        if (name.empty()) return;

        std::string dirPath = "Application/Assets/Scene";
        std::string filePath = dirPath + "/" + sceneName_ + ".json";
        auto& jsonManager = JsonManager::GetInstance();

        jsonManager.CreateJsonDirectory(dirPath);

        json j;
        if (jsonManager.FileExists(filePath)) {
            j = jsonManager.LoadJson(filePath);
        }

        auto* spriteObj = dynamic_cast<SpriteObject*>(obj);
        if (spriteObj) {
            SceneSerializer::SaveSpriteObject(spriteObj, j["objects"][name]);
        } else if (auto* modelObj = dynamic_cast<ModelGameObject*>(obj)) {
            SceneSerializer::SaveObject(modelObj, j["objects"][name]);
        }

        jsonManager.SaveJson(filePath, j);

        if (onSaveNotification_) {
            onSaveNotification_("\"" + name + "\" を保存しました");
        }
    }
}
