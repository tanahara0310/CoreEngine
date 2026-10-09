#include "pch.h"
#include "Editor/Panel/EditorMenuRegistry.h"

#include <imgui.h>

#include <algorithm>
#include <utility>

namespace CoreEngine::Editor
{
    namespace
    {
        /// @brief `/` で区切る（区切りの間が空なら空の配列を返す）
        std::vector<std::string> SplitPath(std::string_view path)
        {
            std::vector<std::string> segments;
            std::size_t begin = 0;
            while (begin <= path.size()) {
                const std::size_t end = (std::min)(path.find('/', begin), path.size());
                if (end == begin) {
                    return {};
                }
                segments.emplace_back(path.substr(begin, end - begin));
                begin = end + 1;
            }
            return segments;
        }
    }

    EditorMenuRegistry& EditorMenuRegistry::Get()
    {
        static EditorMenuRegistry instance;
        return instance;
    }

    ScopedRegistration EditorMenuRegistry::Register(EditorMenuItemDesc desc)
    {
        std::vector<std::string> segments = SplitPath(desc.path);
        if (segments.size() < 2) {
            return {};
        }

        const uint64_t registration = ++lastRegistration_;
        entries_.push_back(Entry{ std::move(desc), std::move(segments), registration });
        return ScopedRegistration([this, registration] {
            std::erase_if(entries_, [registration](const Entry& entry) { return entry.registration == registration; });
        });
    }

    void EditorMenuRegistry::DrawItems(std::string_view root)
    {
        std::vector<const Entry*> matched;
        for (const Entry& entry : entries_) {
            if (entry.segments.front() == root) {
                matched.push_back(&entry);
            }
        }
        if (matched.empty()) {
            return;
        }

        ImGui::Separator();
        DrawLevel(matched, 1);
        RunPendingAction();
    }

    void EditorMenuRegistry::DrawExtraMenus(std::span<const std::string_view> builtInRoots)
    {
        std::vector<std::string_view> roots;
        for (const Entry& entry : entries_) {
            const std::string& root = entry.segments.front();
            const bool builtIn = std::find(builtInRoots.begin(), builtInRoots.end(), root) != builtInRoots.end();
            if (!builtIn && std::find(roots.begin(), roots.end(), root) == roots.end()) {
                roots.push_back(root);
            }
        }

        for (const std::string_view root : roots) {
            const std::string label(root);
            if (!ImGui::BeginMenu(label.c_str())) {
                continue;
            }
            std::vector<const Entry*> matched;
            for (const Entry& entry : entries_) {
                if (entry.segments.front() == root) {
                    matched.push_back(&entry);
                }
            }
            DrawLevel(matched, 1);
            ImGui::EndMenu();
        }
        RunPendingAction();
    }

    void EditorMenuRegistry::DrawLevel(const std::vector<const Entry*>& entries, std::size_t depth)
    {
        std::vector<std::string_view> drawnSubmenus;
        for (const Entry* entry : entries) {
            const std::string& name = entry->segments[depth];
            if (entry->segments.size() == depth + 1) {
                const bool checked = entry->desc.checked && entry->desc.checked();
                if (ImGui::MenuItem(name.c_str(), nullptr, checked) && entry->desc.action) {
                    pendingAction_ = entry->desc.action;
                }
                continue;
            }

            if (std::find(drawnSubmenus.begin(), drawnSubmenus.end(), name) != drawnSubmenus.end()) {
                continue;
            }
            drawnSubmenus.push_back(name);
            if (!ImGui::BeginMenu(name.c_str())) {
                continue;
            }
            std::vector<const Entry*> children;
            for (const Entry* candidate : entries) {
                if (candidate->segments.size() > depth + 1 && candidate->segments[depth] == name) {
                    children.push_back(candidate);
                }
            }
            DrawLevel(children, depth + 1);
            ImGui::EndMenu();
        }
    }

    void EditorMenuRegistry::RunPendingAction()
    {
        if (std::function<void()> action = std::exchange(pendingAction_, nullptr)) {
            action();
        }
    }
}
