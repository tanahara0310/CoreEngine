#include "pch.h"
#include "Editor/Scene/SceneFileDialogs.h"

#ifdef CORE_EDITOR

#include "Editor/ImGui/EditorTheme.h"
#include "Editor/Scene/SceneDebugEditor.h"
#include "Scene/SceneSaveSystem.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <string>
#include <vector>

namespace CoreEngine::Editor
{
    namespace
    {
        constexpr const char* kConflictTitle = "ほかの人の変更とぶつかります";
        constexpr const char* kExternalTitle = "外でシーンのファイルが変わりました";

        /// 窓の幅（文字の大きさの倍数）
        constexpr float kWidthInFonts = 38.0f;

        /// 一覧の高さの上限（行数）
        constexpr float kMaxListRows = 8.0f;

        /// @brief ボタンの幅
        float ButtonWidth(const char* label)
        {
            return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        }

        /// @brief 右寄せでボタンを並べる位置へ寄せる
        void AlignButtons(float width)
        {
            const float x = ImGui::GetContentRegionMax().x - width;
            if (x > ImGui::GetCursorPosX()) {
                ImGui::SetCursorPosX(x);
            }
        }

        /// @brief 窓を画面の中央に、決まった幅で出す
        void PlaceWindow()
        {
            const float width = ImGui::GetFontSize() * kWidthInFonts;
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, FLT_MAX));
        }

        /// @brief ファイルの一覧（多ければ中でスクロールする）
        void DrawFileList(const std::vector<std::string>& lines)
        {
            const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
            const float rows = (std::min)(static_cast<float>(lines.size()), kMaxListRows);
            if (ImGui::BeginChild("##Files", ImVec2(0.0f, rowHeight * rows + ImGui::GetStyle().WindowPadding.y * 2.0f),
                    ImGuiChildFlags_Borders)) {
                for (const std::string& line : lines) {
                    ImGui::TextUnformatted(line.c_str());
                }
            }
            ImGui::EndChild();
        }

        /// @brief 変わり方の名前
        const char* KindLabel(SceneSaveSystem::ExternalChange::Kind kind)
        {
            switch (kind) {
            case SceneSaveSystem::ExternalChange::Kind::Added:   return "足された";
            case SceneSaveSystem::ExternalChange::Kind::Removed: return "消された";
            default:                                             return "変わった";
            }
        }
    }

    void SceneFileDialogs::Draw(SceneDebugEditor* editor)
    {
        if (!editor) {
            return;
        }
        if (!editor->GetSaveConflicts().empty()) {
            DrawSaveConflicts(*editor);
            return;
        }
        if (!editor->GetExternalChanges().empty()) {
            DrawExternalChanges(*editor);
        }
    }

    void SceneFileDialogs::DrawSaveConflicts(SceneDebugEditor& editor)
    {
        if (!ImGui::IsPopupOpen(kConflictTitle)) {
            ImGui::OpenPopup(kConflictTitle);
        }
        PlaceWindow();
        if (!ImGui::BeginPopupModal(kConflictTitle, nullptr,
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
            return;
        }

        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted("保存すると、外（git の pull など）で変わった次のファイルを、自分の値で上書きします。"
                               "ほかの人がそのファイルに加えた変更は消えます。");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        DrawFileList(editor.GetSaveConflicts());
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(Theme::kTextMute, "%s",
            "ほかの人の変更を残すなら「読み直す」を選びます（保存していない自分の変更は消えます）。");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();

        const char* cancelLabel = "保存をやめる";
        const char* reloadLabel = "読み直す";
        const char* overwriteLabel = "上書きして保存";
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        AlignButtons(ButtonWidth(cancelLabel) + ButtonWidth(reloadLabel) + ButtonWidth(overwriteLabel) + spacing * 2.0f);
        if (ImGui::Button(cancelLabel)) {
            editor.CancelSaveConflicts();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(reloadLabel)) {
            ImGui::CloseCurrentPopup();
            editor.ReloadFromDisk();
        }
        ImGui::SameLine();
        if (ImGui::Button(overwriteLabel)) {
            ImGui::CloseCurrentPopup();
            editor.SaveSceneOverwriting();
        }
        ImGui::EndPopup();
    }

    void SceneFileDialogs::DrawExternalChanges(SceneDebugEditor& editor)
    {
        if (!ImGui::IsPopupOpen(kExternalTitle)) {
            ImGui::OpenPopup(kExternalTitle);
        }
        PlaceWindow();
        if (!ImGui::BeginPopupModal(kExternalTitle, nullptr,
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
            return;
        }

        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted("git の pull などで、開いているシーンのファイルが外で変わりました。"
                               "このシーンには、保存していない変更があります。");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();

        std::vector<std::string> lines;
        for (const SceneSaveSystem::ExternalChange& change : editor.GetExternalChanges()) {
            lines.push_back(std::string(KindLabel(change.kind)) + "  " + change.fileName);
        }
        DrawFileList(lines);
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(Theme::kTextMute, "%s",
            "続けて保存すると、自分が変えていないファイルは外のものを残します。"
            "自分も変えたファイルは、保存のときに確かめます。");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();

        const char* keepLabel = "このまま続ける";
        const char* reloadLabel = "読み直す（自分の変更は捨てる）";
        AlignButtons(ButtonWidth(keepLabel) + ButtonWidth(reloadLabel) + ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::Button(keepLabel)) {
            editor.KeepEditingDespiteExternalChanges();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(reloadLabel)) {
            ImGui::CloseCurrentPopup();
            editor.ReloadFromDisk();
        }
        ImGui::EndPopup();
    }
}

#endif // CORE_EDITOR
