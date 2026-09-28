#include "pch.h"
#include "Editor/Export/GameExportDialog.h"

#ifdef CORE_EDITOR

#include "Editor/ImGui/EditorTheme.h"

#include <imgui.h>

#include <Windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <format>
#include <string>

namespace CoreEngine::Editor
{
    namespace
    {
        constexpr const char* kTitle = "ゲームを書き出す";

        /// @brief path を表示用の UTF-8 にする
        std::string ToUtf8(const std::filesystem::path& path)
        {
            const std::u8string text = path.u8string();
            return std::string(text.begin(), text.end());
        }

        /// @brief ボタンの幅
        float ButtonWidth(const char* label)
        {
            return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        }

        /// @brief 見出しと値の 1 行
        void Row(const char* label, const std::string& value)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(Theme::kTextMute, "%s", label);
            ImGui::TableSetColumnIndex(1);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(value.c_str());
            ImGui::PopTextWrapPos();
        }
    }

    void GameExportDialog::Open(bool sceneDirty)
    {
        if (state_ == State::Exporting) {
            return;
        }
        plan_ = GameExporter::Plan();
        sceneDirty_ = sceneDirty;
        result_ = {};
        state_ = State::Ready;
        requestOpen_ = true;
    }

    void GameExportDialog::Draw()
    {
        if (state_ == State::Closed) {
            return;
        }
        if (requestOpen_) {
            ImGui::OpenPopup(kTitle);
            requestOpen_ = false;
        }

        // 書き出しが終わったら結果を受け取る
        if (state_ == State::Exporting && task_.valid() &&
            task_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            result_ = task_.get();
            state_ = State::Done;
        }

        const float fontSize = ImGui::GetFontSize();
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(fontSize * 44.0f, 0.0f), ImVec2(fontSize * 44.0f, FLT_MAX));

        // 書き出している間は閉じさせない
        bool open = true;
        const bool visible = ImGui::BeginPopupModal(kTitle, state_ == State::Exporting ? nullptr : &open,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
        if (!visible) {
            if (!ImGui::IsPopupOpen(kTitle) && state_ != State::Exporting) {
                state_ = State::Closed;
            }
            return;
        }

        switch (state_) {
        case State::Ready:     DrawReady(); break;
        case State::Exporting: DrawExporting(); break;
        case State::Done:      DrawDone(); break;
        default: break;
        }
        if (state_ == State::Closed) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void GameExportDialog::DrawReady()
    {
        const float fontSize = ImGui::GetFontSize();
        if (ImGui::BeginTable("##Plan", 2)) {
            ImGui::TableSetupColumn("##Label", ImGuiTableColumnFlags_WidthFixed, fontSize * 7.0f);
            ImGui::TableSetupColumn("##Value", ImGuiTableColumnFlags_WidthStretch);
            Row("書き出す先", ToUtf8(plan_.destination));
            Row("Release の exe", plan_.error.empty()
                ? ToUtf8(plan_.releaseExe) + "（ビルド " + plan_.releaseBuiltAt + "）"
                : std::string("－"));
            Row("写すもの", "Release のフォルダの exe と DLL\nEngine\\Assets・Application\\Assets・Application\\Config");
            ImGui::EndTable();
        }
        ImGui::Spacing();

        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(Theme::kTextMute, "%s",
            "書き出す先の Engine\\Assets・Application\\Assets・Application\\Config は、消してから写し直します。"
            "ゲームが書く Application\\Saved は残します。");
        if (sceneDirty_) {
            ImGui::TextColored(Theme::kWarn, "%s", "保存していない変更は書き出しに入りません。先にシーンを保存してください。");
        }
        if (!plan_.error.empty()) {
            ImGui::TextColored(Theme::kError, "%s", plan_.error.c_str());
        }
        ImGui::PopTextWrapPos();
        ImGui::Spacing();

        const char* cancelLabel = "やめる";
        const char* exportLabel = "書き出す";
        AlignButtons(ButtonWidth(cancelLabel) + ButtonWidth(exportLabel) + ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::Button(cancelLabel)) {
            state_ = State::Closed;
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!plan_.error.empty());
        ImGui::PushStyleColor(ImGuiCol_Button, Theme::kAccent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::kAccentHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::kAccentMuted);
        if (ImGui::Button(exportLabel)) {
            progress_ = std::make_unique<ExportProgress>();
            task_ = std::async(std::launch::async, [plan = plan_, progress = progress_.get()]() {
                return GameExporter::Export(plan, *progress);
                });
            state_ = State::Exporting;
        }
        ImGui::PopStyleColor(3);
        ImGui::EndDisabled();
    }

    void GameExportDialog::DrawExporting()
    {
        const int total = progress_ ? progress_->total.load() : 0;
        const int copied = progress_ ? progress_->copied.load() : 0;
        ImGui::TextUnformatted("書き出しています…");
        const std::string overlay = std::format("{} / {}", copied, total);
        ImGui::ProgressBar(total > 0 ? static_cast<float>(copied) / static_cast<float>(total) : 0.0f,
            ImVec2(-FLT_MIN, 0.0f), overlay.c_str());
    }

    void GameExportDialog::DrawDone()
    {
        ImGui::PushTextWrapPos(0.0f);
        if (!result_.error.empty()) {
            ImGui::TextColored(Theme::kError, "%s", "書き出せませんでした");
            ImGui::TextUnformatted(result_.error.c_str());
        } else {
            const double megabytes = static_cast<double>(result_.bytes) / (1024.0 * 1024.0);
            ImGui::TextColored(Theme::kOk, "%s",
                std::format("書き出しました（ファイル {} 個・{:.1f} MB）", result_.fileCount, megabytes).c_str());
            ImGui::TextColored(Theme::kTextMute, "%s", ToUtf8(plan_.destination).c_str());
        }
        ImGui::PopTextWrapPos();
        ImGui::Spacing();

        const char* explorerLabel = "エクスプローラーで開く";
        const char* closeLabel = "閉じる";
        const bool succeeded = result_.error.empty();
        const float width = (succeeded ? ButtonWidth(explorerLabel) + ImGui::GetStyle().ItemSpacing.x : 0.0f) +
                            ButtonWidth(closeLabel);
        AlignButtons(width);
        if (succeeded) {
            if (ImGui::Button(explorerLabel)) {
                ::ShellExecuteW(nullptr, L"open", plan_.destination.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
            ImGui::SameLine();
        }
        if (ImGui::Button(closeLabel)) {
            state_ = State::Closed;
        }
    }

    void GameExportDialog::AlignButtons(float width)
    {
        const float available = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, available - width));
    }
}

#endif // CORE_EDITOR
