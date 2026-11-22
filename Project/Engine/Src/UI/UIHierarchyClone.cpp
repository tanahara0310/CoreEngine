#include "pch.h"
#include "UI/UIHierarchyClone.h"

#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "Reflection/PropertySerializer.h"
#include "UI/RectTransformComponent.h"

#include <memory>
#include <string>
#include <vector>

namespace CoreEngine
{
    GameObject* CloneUIHierarchy(GameObjectManager& manager, const GameObject& root,
                                 std::unordered_map<ObjectId, GameObject*>* outCopies)
    {
        const RectTransformComponent* const rootRect = root.GetComponent<RectTransformComponent>();
        if (!rootRect) {
            return nullptr;
        }

        // 根を先頭に、子孫をシーンの並びの順に集める
        std::vector<const GameObject*> sources = { &root };
        for (const auto& object : manager.GetAllObjects()) {
            if (!object || object.get() == &root || object->IsMarkedForDestroy()) {
                continue;
            }
            const RectTransformComponent* const rect = object->GetComponent<RectTransformComponent>();
            if (rect && rootRect->IsSelfOrDescendant(rect)) {
                sources.push_back(object.get());
            }
        }

        // 先に全部の器を作って新しい ID を決め、それから互いを指す参照を新しい ID へ付け替えて値を流す
        std::unordered_map<std::string, std::string> ids;
        std::vector<GameObject*> copies;
        copies.reserve(sources.size());
        for (const GameObject* const source : sources) {
            auto owned = std::make_unique<GameObject>();
            owned->SetName(source->GetName());
            owned->SetSerializeEnabled(false);
            GameObject* const copy = manager.AddObject(std::move(owned));
            ids.emplace(source->GetObjectId().ToString(), copy->GetObjectId().ToString());
            copies.push_back(copy);
            if (outCopies) {
                outCopies->emplace(source->GetObjectId(), copy);
            }
        }

        for (std::size_t i = 0; i < sources.size(); ++i) {
            json state = sources[i]->Serialize();
            Reflection::PropertySerializer::RemapObjectRefs(state, ids);
            copies[i]->Deserialize(state);
        }
        manager.InvalidateReferences();
        return copies.front();
    }
}
