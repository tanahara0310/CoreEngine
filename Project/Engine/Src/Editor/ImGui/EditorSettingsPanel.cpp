#include "pch.h"
#include "EditorSettingsPanel.h"

#ifdef USE_IMGUI

#include "EngineSystem/Settings/EditorSettingsSubsystem.h"
#include "Editor/ImGui/ImGuiAll.h"

namespace CoreEngine
{
    void EditorSettingsPanel::Draw(EditorSettingsSubsystem* store)
    {
        if (!store) {
            ImGui::TextDisabled("EditorSettingsSubsystem がありません");
            return;
        }

        ImGui::TextDisabled("エディタ設定は変更のたびに自動保存されます（1秒間隔の差分検知）");
        ImGui::TextDisabled("保存先: %s", store->GetSettingsDirForDisplay().c_str());
        ImGui::Spacing();

        const auto statuses = store->GetSectionStatuses();
        if (statuses.empty()) {
            ImGui::TextDisabled("登録中のセクションはありません");
            return;
        }

        if (ImGui::BeginTable("EditorSettingsSections", 4,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("セクション");
            ImGui::TableSetupColumn("状態");
            ImGui::TableSetupColumn("最終保存");
            ImGui::TableSetupColumn("操作");
            ImGui::TableHeadersRow();
            for (const auto& status : statuses) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(status.name.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(status.fileExists ? "保存済み" : "未保存（デフォルト）");
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(status.lastSaveTime.empty() ? "-" : status.lastSaveTime.c_str());
                ImGui::TableNextColumn();
                ImGui::PushID(status.name.c_str());
                if (ImGui::SmallButton("リセット")) {
                    store->ResetSectionToDefault(status.name);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("起動時のコードデフォルト値へ戻します\n（直前の保存内容はバックアップへ退避されます）");
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(!status.backupExists);
                if (ImGui::SmallButton("バックアップ復元")) {
                    store->RestoreSectionBackup(status.name);
                }
                ImGui::EndDisabled();
                if (status.backupExists && ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("1世代前の保存内容へ戻します");
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
}

#endif // USE_IMGUI
