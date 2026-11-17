#include "pch.h"
#include "Editor/Inspector/TagField.h"

#ifdef CORE_EDITOR

#include "Editor/ImGui/EditorTheme.h"
#include "Editor/ImGui/ImGuiAll.h"
#include "GameObject/GameObjectTags.h"

namespace CoreEngine::Editor::TagField
{
    bool Draw(std::string& tag)
    {
        const bool defined = GameObjectTags::IsDefined(tag);
        const std::string preview = defined ? tag : tag + "（一覧に無い）";

        ImGui::SetNextItemWidth(-FLT_MIN);
        if (!defined) {
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::kWarm);
        }
        const bool open = ImGui::BeginCombo("##tag", preview.c_str());
        if (!defined) {
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("プロジェクト設定の Tags に無いタグです");
            }
        }
        if (!open) {
            return false;
        }

        bool changed = false;
        for (const std::string& name : GameObjectTags::Names()) {
            const bool selected = name == tag;
            if (ImGui::Selectable(name.c_str(), selected) && !selected) {
                tag = name;
                changed = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::Separator();
        ImGui::TextDisabled("タグは Edit ▸ Project Settings… の Tags で足します");
        ImGui::EndCombo();
        return changed;
    }
}

#endif // CORE_EDITOR
