#include "SceneSerializer.h"
#include "ObjectCommon/GameObject.h"
#include "ObjectCommon/Sprite/SpriteObject.h"
#include "WorldTransform/WorldTransform.h"

namespace CoreEngine
{
    void SceneSerializer::SaveObject(const GameObject* obj, json& j)
    {
        if (!obj) return;

        j["active"] = obj->IsActive();

        const WorldTransform& t = obj->GetTransform();
        j["transform"]["translate"] = JsonManager::Vector3ToJson(t.translate);
        j["transform"]["rotate"] = JsonManager::Vector3ToJson(t.rotate);
        j["transform"]["scale"] = JsonManager::Vector3ToJson(t.scale);
    }

    void SceneSerializer::LoadObject(GameObject* obj, const json& j)
    {
        if (!obj) return;

        if (j.contains("active")) {
            obj->SetActive(j["active"].get<bool>());
        }

        if (j.contains("transform")) {
            const json& t = j["transform"];
            WorldTransform& transform = obj->GetTransform();
            transform.translate = JsonManager::SafeGetVector3(t, "translate", transform.translate);
            transform.rotate = JsonManager::SafeGetVector3(t, "rotate", transform.rotate);
            transform.scale = JsonManager::SafeGetVector3(t, "scale", transform.scale);
        }
    }

    void SceneSerializer::SaveSpriteObject(const SpriteObject* obj, json& j)
    {
        if (!obj) return;

        j["active"] = obj->IsActive();

        const EulerTransform& t = obj->GetSpriteTransform();
        j["transform"]["translate"] = JsonManager::Vector3ToJson(t.translate);
        j["transform"]["rotate"] = JsonManager::Vector3ToJson(t.rotate);
        j["transform"]["scale"] = JsonManager::Vector3ToJson(t.scale);
    }

    void SceneSerializer::LoadSpriteObject(SpriteObject* obj, const json& j)
    {
        if (!obj) return;

        if (j.contains("active")) {
            obj->SetActive(j["active"].get<bool>());
        }

        if (j.contains("transform")) {
            const json& t = j["transform"];
            EulerTransform& transform = obj->GetSpriteTransform();
            transform.translate = JsonManager::SafeGetVector3(t, "translate", transform.translate);
            transform.rotate = JsonManager::SafeGetVector3(t, "rotate", transform.rotate);
            transform.scale = JsonManager::SafeGetVector3(t, "scale", transform.scale);
        }
    }
}
