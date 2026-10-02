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

    ScopedRegistration EditorPanelRegistry::Register(EditorPanelDesc desc)
    {
        const uint64_t registration = ++lastRegistration_;
        const std::string id = desc.id;

        if (EditorPanel* existing = Find(id)) {
            // 表示状態は残したまま中身だけ差し替える
            existing->desc = std::move(desc);
            existing->registration_ = registration;
        } else {
            auto panel = std::make_unique<EditorPanel>();
            panel->desc = std::move(desc);
            panel->registration_ = registration;
            panel->visible = panel->desc.defaultVisible;

            // 外したときの開閉状態、無ければ前回の開閉状態を当てる
            if (const auto it = unregisteredVisibility_.find(id); it != unregisteredVisibility_.end()) {
                panel->visible = it->second;
                unregisteredVisibility_.erase(it);
            } else if (const auto saved = savedVisibility_.find(id); saved != savedVisibility_.end()) {
                panel->visible = saved->second;
            }
            EditorPanel& ref = *panel;
            panels_.push_back(std::move(panel));

            if (ref.desc.placement == PanelPlacement::Window && dockRegistrar_) {
                dockRegistrar_(ref.desc);
            }
        }

        return ScopedRegistration([this, id, registration] { Unregister(id, registration); });
    }

    void EditorPanelRegistry::Unregister(const std::string& id, uint64_t registration)
    {
        const auto it = std::find_if(panels_.begin(), panels_.end(),
            [&](const std::unique_ptr<EditorPanel>& panel) {
                return panel && panel->desc.id == id && panel->registration_ == registration;
            });
        if (it == panels_.end()) {
            return;
        }

        // 開閉を覚えるパネルは、外したときの状態を次の登録と保存へ回す
        if (IsVisibilityPersisted(**it)) {
            unregisteredVisibility_[id] = (*it)->visible;
        }
        panels_.erase(it);
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
        for (const auto& [id, visible] : savedVisibility_) {
            unregisteredVisibility_.erase(id);
        }
        for (auto& panel : panels_) {
            if (!panel || !IsVisibilityPersisted(*panel)) { continue; }
            if (const auto it = savedVisibility_.find(panel->desc.id);
                it != savedVisibility_.end()) {
                panel->visible = it->second;
            }
        }
    }

    void EditorPanelRegistry::ForEachPersistedVisibility(
        const std::function<void(const std::string& id, bool visible)>& fn) const
    {
        if (!fn) { return; }
        for (const auto& panel : panels_) {
            if (panel && IsVisibilityPersisted(*panel)) {
                fn(panel->desc.id, panel->visible);
            }
        }
        for (const auto& [id, visible] : unregisteredVisibility_) {
            fn(id, visible);
        }
    }
}
