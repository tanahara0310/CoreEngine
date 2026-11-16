#pragma once

#ifdef CORE_EDITOR

#include "Editor/ImGui/EditorTheme.h"
#include "Editor/ImGui/Wrappers/ImGuiLayout.h"

#include <imgui.h>

#include <functional>
#include <string>

/// @file
/// @brief 入力して決めるモーダル（訳の赤字・決めるボタン・やめるボタン付き）

namespace CoreEngine::UI
{
    /// @brief 入力して決めるモーダルの設定
    struct ModalDialog
    {
        const char* title = "";       ///< 窓の題（ポップアップの ID も兼ねる）
        float width = 420.0f;         ///< 最初に開いたときの幅
        const char* commitLabel = ""; ///< 決めるボタンの文字
        bool* show = nullptr;         ///< 開く合図（閉じたら false にする）
        std::string* error = nullptr; ///< 決められなかった訳（空なら出さない。やめたら空にする）
    };

    /// @brief 開く合図が立っていればモーダルを開き、中身・訳・決めるボタン・やめるボタンを描く
    /// @param drawBody 中身を描く処理。入力欄で Enter を押して決めたら true を返す
    /// @param commit 決めるボタンか Enter で呼ぶ処理。閉じてよければ true を返す
    /// @param onClosed 決めて閉じた後に呼ぶ処理（無ければ空）
    inline void DrawModalDialog(const ModalDialog& dialog, const std::function<bool()>& drawBody,
        const std::function<bool()>& commit, const std::function<void()>& onClosed = {})
    {
        if (*dialog.show && !ImGui::IsPopupOpen(dialog.title)) {
            ImGui::OpenPopup(dialog.title);
        }

        ImGui::SetNextWindowSize(ImVec2(dialog.width, 0.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::BeginPopupModal(dialog.title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }

        const bool enterPressed = drawBody();

        if (!dialog.error->empty()) {
            ImGui::TextColored(Editor::Theme::kError, "%s", dialog.error->c_str());
        }

        UI::Separator();
        if (ImGui::Button(dialog.commitLabel) || enterPressed) {
            if (commit()) {
                *dialog.show = false;
                ImGui::CloseCurrentPopup();
                if (onClosed) {
                    onClosed();
                }
            }
        }
        UI::SameLine();
        if (ImGui::Button("やめる")) {
            *dialog.show = false;
            dialog.error->clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

#endif // CORE_EDITOR
