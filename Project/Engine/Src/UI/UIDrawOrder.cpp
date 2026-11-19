#include "pch.h"
#include "UI/UIDrawOrder.h"

#include "GameObject/GameObjectManager.h"
#include "UI/RectTransformComponent.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace CoreEngine
{
    void ResolveUIDrawOrder(GameObjectManager& manager, bool& resolved)
    {
        if (!RectTransformComponent::AnyHasParent() && !resolved) {
            return;
        }

        std::vector<RectTransformComponent*> items;
        bool anyParent = false;
        manager.ForEachComponent<RectTransformComponent>([&](RectTransformComponent& rect) {
            items.push_back(&rect);
            anyParent = anyParent || rect.GetParent() != nullptr;
            });

        if (!anyParent) {
            for (RectTransformComponent* const rect : items) {
                rect->ApplyRenderOrder();
            }
            resolved = false;
            return;
        }
        resolved = true;

        const int count = static_cast<int>(items.size());
        std::unordered_map<const RectTransformComponent*, int> indexOf;
        indexOf.reserve(items.size());
        for (int i = 0; i < count; ++i) {
            indexOf.emplace(items[i], i);
        }

        // 親の番号（親が無い・描かない親なら -1）と、親ごとの子の並び
        std::vector<int> parentOf(items.size(), -1);
        std::vector<std::vector<int>> children(items.size());
        std::vector<int> roots;
        for (int i = 0; i < count; ++i) {
            const RectTransformComponent* const parent = items[i]->GetParent();
            const auto found = parent ? indexOf.find(parent) : indexOf.end();
            if (found != indexOf.end()) {
                parentOf[i] = found->second;
                children[found->second].push_back(i);
            } else {
                roots.push_back(i);
            }
        }

        const auto bySortOrder = [&](int a, int b) {
            return items[a]->GetSortOrder() < items[b]->GetSortOrder();
        };
        std::stable_sort(roots.begin(), roots.end(), bySortOrder);
        for (std::vector<int>& list : children) {
            std::stable_sort(list.begin(), list.end(), bySortOrder);
        }

        // 根から深さ優先でたどり、たどった順に描く順を振る
        std::vector<bool> visited(items.size(), false);
        std::vector<int> stack;
        int sequence = 0;
        const auto walk = [&](int root) {
            stack.push_back(root);
            while (!stack.empty()) {
                const int index = stack.back();
                stack.pop_back();
                if (visited[index]) {
                    continue;
                }
                visited[index] = true;
                items[index]->ApplyResolvedRenderOrder(RectTransformComponent::kRenderOrderBase + sequence++);
                const std::vector<int>& list = children[index];
                for (auto it = list.rbegin(); it != list.rend(); ++it) {
                    stack.push_back(*it);
                }
            }
        };
        for (const int root : roots) {
            walk(root);
        }

        // 親をたどると輪になっている物は、輪の中で最初の物から描く
        for (int i = 0; i < count; ++i) {
            if (!visited[i]) {
                walk(i);
            }
        }
    }
}
