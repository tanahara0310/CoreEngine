#include "pch.h"
#include "Editor/Panel/EditorPanelRegistry.h"

#include <algorithm>

namespace CoreEngine::Editor
{
    const char* ToDisplayName(PanelGroup group)
    {
        for (const auto& [value, label] : kPanelGroupOrder) {
            if (value == group) {
                return label;
            }
        }
        return group == PanelGroup::Application ? "Application" : "Other";
    }

    EditorPanelRegistry& EditorPanelRegistry::Get()
    {
        static EditorPanelRegistry instance;
        return instance;
    }

    EditorPanel& EditorPanelRegistry::Register(EditorPanelDesc desc)
    {
        if (EditorPanel* existing = Find(desc.id)) {
            // 表示状態は残したまま中身だけ差し替える
            // （シーンを切り替えても開いていたパネルが閉じないようにする）
            existing->desc = std::move(desc);
            return *existing;
        }

        auto panel = std::make_unique<EditorPanel>();
        panel->desc = std::move(desc);
        panel->visible = panel->desc.defaultVisible;

        // 前回の開閉状態が残っていればそちらを優先する
        if (const auto it = savedVisibility_.find(panel->desc.id); it != savedVisibility_.end()) {
            panel->visible = it->second;
        }
        EditorPanel& ref = *panel;
        panels_.push_back(std::move(panel));

        if (ref.desc.placement == PanelPlacement::Window && dockRegistrar_) {
            dockRegistrar_(ref.desc);
        }
        return ref;
    }

    void EditorPanelRegistry::Unregister(const std::string& id, const void* owner)
    {
        std::erase_if(panels_, [&](const std::unique_ptr<EditorPanel>& panel) {
            return panel && panel->desc.id == id
                && (owner == nullptr || panel->desc.owner == owner);
            });
    }

    EditorPanel* EditorPanelRegistry::Find(const std::string& id)
    {
        for (auto& panel : panels_) {
            if (panel && panel->desc.id == id) {
                return panel.get();
            }
        }
        return nullptr;
    }

    void EditorPanelRegistry::ForEach(PanelPlacement placement,
                                      const std::function<void(EditorPanel&)>& fn)
    {
        if (!fn) { return; }
        for (auto& panel : panels_) {
            if (panel && panel->desc.placement == placement) {
                fn(*panel);
            }
        }
    }

    bool EditorPanelRegistry::Any(PanelPlacement placement,
                                  const std::function<bool(const EditorPanel&)>& pred) const
    {
        for (const auto& panel : panels_) {
            if (!panel || panel->desc.placement != placement) { continue; }
            if (!pred || pred(*panel)) { return true; }
        }
        return false;
    }

    EditorPanel* EditorPanelRegistry::FindFirst(PanelPlacement placement)
    {
        for (auto& panel : panels_) {
            if (panel && panel->desc.placement == placement) {
                return panel.get();
            }
        }
        return nullptr;
    }

    void EditorPanelRegistry::SetDockRegistrar(DockRegistrar registrar)
    {
        dockRegistrar_ = std::move(registrar);
        if (!dockRegistrar_) { return; }

        // 橋渡しが付く前に登録されたぶんをまとめて通知する
        for (const auto& panel : panels_) {
            if (panel && panel->desc.placement == PanelPlacement::Window) {
                dockRegistrar_(panel->desc);
            }
        }
    }

    bool EditorPanelRegistry::IsVisibilityPersisted(const EditorPanel& panel)
    {
        // Settings セクションと Hierarchy の中身は選択式で開閉の概念が無い
        return panel.desc.placement == PanelPlacement::Window
            || panel.desc.placement == PanelPlacement::InspectorTab;
    }

    void EditorPanelRegistry::ApplySavedVisibility(std::unordered_map<std::string, bool> saved)
    {
        savedVisibility_ = std::move(saved);
        for (auto& panel : panels_) {
            if (!panel || !IsVisibilityPersisted(*panel)) { continue; }
            if (const auto it = savedVisibility_.find(panel->desc.id);
                it != savedVisibility_.end()) {
                panel->visible = it->second;
            }
        }
    }
}
