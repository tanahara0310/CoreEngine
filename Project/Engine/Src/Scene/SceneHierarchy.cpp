#include "pch.h"
#include "Scene/SceneHierarchy.h"

#include "GameObject/Component/Transform/TransformComponent.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "UI/RectTransformComponent.h"

#include <algorithm>
#include <utility>

namespace CoreEngine::SceneHierarchy
{
    namespace
    {
        /// 親をたどる段数の上限（壊れた循環があっても止まる）
        constexpr int kMaxDepth = 256;

        /// @brief object から親をたどって root に着くまでの段数（root の子孫でなければ 0）
        int DepthBelow(const GameObject& object, const GameObject& root)
        {
            const GameObject* node = FindParent(object);
            for (int depth = 1; node && depth <= kMaxDepth; ++depth) {
                if (node == &root) {
                    return depth;
                }
                node = FindParent(*node);
            }
            return 0;
        }
    }

    GameObject* FindParent(const GameObject& object)
    {
        if (const TransformComponent* const transform = object.GetComponent<TransformComponent>()) {
            const TransformComponent* const parent = transform->GetParent();
            return parent ? parent->GetOwner() : nullptr;
        }
        if (const RectTransformComponent* const rect = object.GetComponent<RectTransformComponent>()) {
            const RectTransformComponent* const parent = rect->GetParent();
            return parent ? parent->GetOwner() : nullptr;
        }
        return nullptr;
    }

    bool IsDescendantOf(const GameObject& object, const GameObject& root)
    {
        return DepthBelow(object, root) > 0;
    }

    std::vector<GameObject*> CollectDescendants(const GameObjectManager& manager, const GameObject& root)
    {
        std::vector<std::pair<int, GameObject*>> found;
        for (const auto& candidate : manager.GetAllObjects()) {
            if (!candidate || candidate.get() == &root || candidate->IsMarkedForDestroy()) {
                continue;
            }
            if (const int depth = DepthBelow(*candidate, root); depth > 0) {
                found.emplace_back(depth, candidate.get());
            }
        }
        std::stable_sort(found.begin(), found.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

        std::vector<GameObject*> descendants;
        descendants.reserve(found.size());
        for (const auto& [depth, object] : found) {
            descendants.push_back(object);
        }
        return descendants;
    }

    void MarkDescendantsOfDestroyed(GameObjectManager& manager)
    {
        const auto& objects = manager.GetAllObjects();
        const bool anyDestroyed = std::any_of(objects.begin(), objects.end(),
            [](const auto& object) { return object && object->IsMarkedForDestroy(); });
        if (!anyDestroyed) {
            return;
        }

        // 親に印が付いていれば自分にも付ける。孫より先まで届くよう、増えなくなるまで繰り返す
        for (bool changed = true; changed;) {
            changed = false;
            for (const auto& object : objects) {
                if (!object || object->IsMarkedForDestroy()) {
                    continue;
                }
                const GameObject* const parent = FindParent(*object);
                if (parent && parent->IsMarkedForDestroy()) {
                    object->Destroy();
                    changed = true;
                }
            }
        }
    }
}
