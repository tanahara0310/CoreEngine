#include "pch.h"
#include "Editor/Launcher/ProjectBrowser.h"

#ifdef CORE_EDITOR

#include "Editor/ImGui/EditorTheme.h"
#include "Editor/Launcher/ProjectThumbnails.h"
#include "Utility/Path/ProjectPaths.h"

#include <shellapi.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <format>
#include <string_view>
#include <utility>

namespace CoreEngine::Editor
{
    namespace
    {
        namespace Theme = Editor::Theme;

        /// @brief path を表示用の UTF-8 にする（区切りは Windows の '\'）
        std::string ToDisplay(const std::filesystem::path& path)
        {
            const std::u8string text = path.u8string();
            return std::string(text.begin(), text.end());
        }

        /// @brief UTF-8 の文字列を path にする
        std::filesystem::path FromUtf8(const std::string& text)
        {
            return std::filesystem::path(std::u8string(text.begin(), text.end()));
        }

        /// @brief UTF-8 の文字列を入力欄の固定長の場所へ写す（入り切らない分は落とす）
        template <size_t N>
        void CopyToBuffer(char (&buffer)[N], const std::string& text)
        {
            const size_t length = std::min(text.size(), N - 1);
            std::memcpy(buffer, text.data(), length);
            buffer[length] = '\0';
        }

        /// @brief ASCII の英字だけ大文字小文字を無視して、text が query を含むか
        bool ContainsIgnoreCase(std::string_view text, std::string_view query)
        {
            if (query.empty()) {
                return true;
            }
            const auto lower = [](char c) {
                return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
                };
            const auto found = std::search(text.begin(), text.end(), query.begin(), query.end(),
                [&lower](char a, char b) { return lower(a) == lower(b); });
            return found != text.end();
        }

        /// @brief 開いた日時（"2026-09-26T12:00:00"）を一覧の表示にする
        std::string FormatLastOpened(const std::string& text)
        {
            int year = 0;
            int month = 0;
            int day = 0;
            int hour = 0;
            int minute = 0;
            int second = 0;
            if (text.empty() ||
                sscanf_s(text.c_str(), "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6) {
                return "－";
            }

            using namespace std::chrono;
            const zoned_time now{ current_zone(), floor<seconds>(system_clock::now()) };
            const year_month_day today{ floor<days>(now.get_local_time()) };
            const year_month_day yesterday{ local_days{ today } - days{ 1 } };
            const year_month_day opened{ std::chrono::year{ year }, std::chrono::month{ static_cast<unsigned>(month) },
                                         std::chrono::day{ static_cast<unsigned>(day) } };

            if (opened == today) {
                return std::format("今日 {:02}:{:02}", hour, minute);
            }
            if (opened == yesterday) {
                return std::format("昨日 {:02}:{:02}", hour, minute);
            }
            if (opened.year() == today.year()) {
                return std::format("{}月{}日", month, day);
            }
            return std::format("{}年{}月{}日", year, month, day);
        }

        /// @brief 名前の最初の 1 文字（UTF-8 の 1 文字分。英字は大文字）
        std::string FirstLetter(const std::string& name)
        {
            if (name.empty()) {
                return "?";
            }
            const unsigned char lead = static_cast<unsigned char>(name.front());
            size_t length = 1;
            if (lead >= 0xF0) { length = 4; }
            else if (lead >= 0xE0) { length = 3; }
            else if (lead >= 0xC0) { length = 2; }
            std::string letter = name.substr(0, std::min(length, name.size()));
            if (letter.size() == 1 && letter[0] >= 'a' && letter[0] <= 'z') {
                letter[0] = static_cast<char>(letter[0] - 'a' + 'A');
            }
            return letter;
        }

        /// @brief サムネイルの代わりに描く面の色（名前ごとに決まる）
        ImVec4 TileColor(const std::string& name)
        {
            static const ImVec4 kPalette[] = {
                Theme::FromSrgb(52, 84, 122),
                Theme::FromSrgb(84, 64, 120),
                Theme::FromSrgb(40, 98, 88),
                Theme::FromSrgb(122, 80, 44),
                Theme::FromSrgb(104, 50, 64),
                Theme::FromSrgb(62, 74, 96),
            };
            size_t hash = 0;
            for (const char c : name) {
                hash = hash * 31 + static_cast<unsigned char>(c);
            }
            return kPalette[hash % std::size(kPalette)];
        }

        /// @brief 色の面と、真ん中に 1 文字を描く
        void DrawTile(ImDrawList* drawList, const ImVec2& min, const ImVec2& size, const ImVec4& color,
                      const std::string& letter, const ImVec4& textColor, bool outline, float letterScale)
        {
            const ImVec2 max(min.x + size.x, min.y + size.y);
            drawList->AddRectFilled(min, max, ImGui::GetColorU32(color), 3.0f);
            if (outline) {
                drawList->AddRect(min, max, ImGui::GetColorU32(Theme::kOutline), 3.0f);
            }

            const float fontSize = ImGui::GetFontSize() * letterScale;
            const ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, letter.c_str());
            const ImVec2 textPos(min.x + (size.x - textSize.x) * 0.5f, min.y + (size.y - textSize.y) * 0.5f);
            drawList->AddText(ImGui::GetFont(), fontSize, textPos, ImGui::GetColorU32(textColor), letter.c_str());
        }

        /// @brief サムネイルを描く（無ければ色の面と名前の頭文字を描く）
        void DrawThumbnail(ImDrawList* drawList, const ImVec2& min, const ImVec2& size,
                           const ProjectEntry& entry, ImTextureID texture, float letterScale)
        {
            if (texture) {
                const ImVec2 max(min.x + size.x, min.y + size.y);
                drawList->AddImageRounded(texture, min, max, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                    IM_COL32_WHITE, 3.0f);
                return;
            }
            if (entry.missing) {
                DrawTile(drawList, min, size, Theme::kField, "?", Theme::kTextMute, true, letterScale);
            } else {
                DrawTile(drawList, min, size, TileColor(entry.name), FirstLetter(entry.name), Theme::kText, false, letterScale);
            }
        }

        /// @brief フォルダを選ぶ窓を出す（選ばれなければ空）
        std::filesystem::path PickFolder(HWND owner, const wchar_t* title)
        {
            Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
            if (FAILED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
                return {};
            }
            DWORD options = 0;
            dialog->GetOptions(&options);
            dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
            dialog->SetTitle(title);
            if (FAILED(dialog->Show(owner))) {
                return {};
            }
            Microsoft::WRL::ComPtr<IShellItem> item;
            if (FAILED(dialog->GetResult(&item))) {
                return {};
            }
            PWSTR path = nullptr;
            if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) || !path) {
                return {};
            }
            std::filesystem::path result(path);
            ::CoTaskMemFree(path);
            return result;
        }

        /// @brief フォルダを選ぶ窓を別のスレッドで出す（そのスレッドで COM をシングルスレッドとして用意する）
        std::future<std::filesystem::path> PickFolderAsync(HWND owner, std::wstring title)
        {
            return std::async(std::launch::async, [owner, title = std::move(title)]() {
                const HRESULT result = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
                std::filesystem::path folder = PickFolder(owner, title.c_str());
                if (SUCCEEDED(result)) {
                    ::CoUninitialize();
                }
                return folder;
                });
        }

        /// 一覧の画面の一言
        constexpr const char* kProjectsHint = "プロジェクトを選んで「開く」を押してください（行のダブルクリックでも開けます）";

        /// 新規作成の画面の一言
        constexpr const char* kNewProjectHint = "テンプレートを選び、名前と保存先を決めて「作成して開く」を押してください";
    }

    ProjectBrowser::ProjectBrowser(ProjectList& list, HWND owner, std::filesystem::path current)
        : list_(list), owner_(owner), current_(std::move(current)),
          engineRootText_(ToDisplay(ProjectPaths::EngineRoot())), status_(kProjectsHint)
    {
        Refresh(current_);
    }

    std::filesystem::path ProjectBrowser::TakeChosen()
    {
        return std::exchange(chosen_, {});
    }

    bool ProjectBrowser::IsCurrent(const ProjectEntry& entry) const
    {
        return !current_.empty() && ProjectList::IsSameFolder(entry.folder, current_);
    }

    ImTextureID ProjectBrowser::ThumbnailOf(const ProjectEntry& entry) const
    {
        return (thumbnails_ && !entry.missing) ? thumbnails_->Get(entry.folder) : ImTextureID{};
    }

    void ProjectBrowser::Refresh(const std::filesystem::path& select)
    {
        std::filesystem::path keep = select;
        if (keep.empty() && selected_ >= 0 && selected_ < static_cast<int>(entries_.size())) {
            keep = entries_[selected_].folder;
        }
        entries_ = list_.Collect();
        selected_ = entries_.empty() ? -1 : 0;
        for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
            if (!keep.empty() && ProjectList::IsSameFolder(entries_[i].folder, keep)) {
                selected_ = i;
                break;
            }
        }
    }

    void ProjectBrowser::SetStatus(std::string text, bool error)
    {
        status_ = std::move(text);
        statusError_ = error;
    }

    std::vector<int> ProjectBrowser::VisibleIndices() const
    {
        std::vector<int> visible;
        for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
            const ProjectEntry& entry = entries_[i];
            if (ContainsIgnoreCase(entry.name, filter_) || ContainsIgnoreCase(ToDisplay(entry.folder), filter_)) {
                visible.push_back(i);
            }
        }
        return visible;
    }

    void ProjectBrowser::Draw()
    {
        FinishPicking();

        DrawNav();
        ImGui::SameLine(0.0f, 0.0f);
        // 右側の地の色は、描く場所に関係なくそろえる
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::kWindow);
        if (view_ == View::NewProject) {
            DrawNewProject();
        } else {
            DrawProjects();
        }
        ImGui::PopStyleColor();

        // 一覧で選んだものを Enter で開く
        if (view_ == View::Projects && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Enter, false) &&
            selected_ >= 0 && selected_ < static_cast<int>(entries_.size())) {
            Request(Action::Open, entries_[selected_].folder);
        }

        RunPendingAction();
    }

    void ProjectBrowser::DrawNav()
    {
        const float fontSize = ImGui::GetFontSize();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::kChild);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(fontSize, fontSize));
        ImGui::BeginChild("##Nav", ImVec2(fontSize * 15.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleVar();

        ImGui::TextColored(Theme::kAccentHover, "CoreEngine");
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextMute);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(engineRootText_.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Selectable("プロジェクト", view_ == View::Projects)) {
            ShowProjects();
        }
        if (ImGui::Selectable("新規作成", view_ == View::NewProject)) {
            ShowNewProject();
        }

        // 下端に「前回のプロジェクトを自動で開く」を置く
        const float bottomHeight = ImGui::GetTextLineHeightWithSpacing() * 3.0f;
        const float bottomY = ImGui::GetWindowHeight() - bottomHeight - fontSize;
        if (ImGui::GetCursorPosY() < bottomY) {
            ImGui::SetCursorPosY(bottomY);
        }
        ImGui::Separator();
        ImGui::Spacing();
        bool openLast = list_.GetOpenLastOnStartup();
        ImGui::PushStyleColor(ImGuiCol_FrameBg, Theme::kControl);
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Theme::kHover);
        const bool toggled = ImGui::Checkbox("##OpenLast", &openLast);
        ImGui::PopStyleColor(2);
        if (toggled) {
            list_.SetOpenLastOnStartup(openLast);
            if (list_.Save()) {
                SetStatus(openLast ? "次の起動から、前回開いたプロジェクトを自動で開きます"
                                   : "次の起動から、この画面を出します");
            } else {
                SetStatus("設定を保存できませんでした", true);
            }
        }
        ImGui::SameLine();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted("次から前回のプロジェクトを自動で開く");
        ImGui::PopTextWrapPos();

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    void ProjectBrowser::DrawProjects()
    {
        const float fontSize = ImGui::GetFontSize();
        const ImGuiStyle& style = ImGui::GetStyle();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(fontSize, fontSize));
        ImGui::BeginChild("##Main", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleVar();

        // 見出しと、絞り込み・追加・新規作成
        const float headerRight = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("プロジェクト");
        const float filterWidth = fontSize * 14.0f;
        const char* addLabel = "追加…";
        const char* newLabel = "＋ 新規作成";
        const float addWidth = ImGui::CalcTextSize(addLabel).x + style.FramePadding.x * 2.0f;
        const float newWidth = ImGui::CalcTextSize(newLabel).x + style.FramePadding.x * 2.0f;
        ImGui::SameLine(headerRight - filterWidth - addWidth - newWidth - style.ItemSpacing.x * 2.0f);
        ImGui::SetNextItemWidth(filterWidth);
        ImGui::InputTextWithHint("##Filter", "名前・場所で絞り込む", filter_, sizeof(filter_));
        ImGui::SameLine();
        if (ImGui::Button(addLabel)) {
            Request(Action::Add, {});
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("ほかの場所にあるプロジェクトのフォルダを選んで、一覧に足します");
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, Theme::kAccent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::kAccentHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::kAccentMuted);
        if (ImGui::Button(newLabel)) {
            ShowNewProject();
        }
        ImGui::PopStyleColor(3);
        ImGui::Spacing();

        // 一覧と詳細
        const float statusHeight = ImGui::GetTextLineHeightWithSpacing() + style.ItemSpacing.y;
        const ImVec2 body(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y - statusHeight);
        const float detailWidth = fontSize * 21.0f;
        const std::vector<int> visible = VisibleIndices();
        if (!visible.empty() && std::find(visible.begin(), visible.end(), selected_) == visible.end()) {
            selected_ = visible.front();
        }
        DrawList(visible, ImVec2(body.x - detailWidth - style.ItemSpacing.x, body.y));
        ImGui::SameLine();
        DrawDetail(ImVec2(detailWidth, body.y));

        DrawStatus();

        ImGui::EndChild();
    }

    void ProjectBrowser::DrawStatus()
    {
        // 状態の一言（件数に重なる分は切る）と件数
        const float fontSize = ImGui::GetFontSize();
        const float statusRight = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        const std::string count = std::format("プロジェクト {} 件", entries_.size());
        const float countWidth = ImGui::CalcTextSize(count.c_str()).x;
        const ImVec2 statusMin = ImGui::GetCursorScreenPos();
        const ImVec2 statusMax(statusMin.x + std::max(0.0f, ImGui::GetContentRegionAvail().x - countWidth - fontSize),
                               statusMin.y + ImGui::GetTextLineHeightWithSpacing());
        ImGui::PushClipRect(statusMin, statusMax, true);
        ImGui::PushStyleColor(ImGuiCol_Text, statusError_ ? Theme::kError : Theme::kTextMute);
        ImGui::TextUnformatted(status_.c_str());
        ImGui::PopStyleColor();
        ImGui::PopClipRect();
        ImGui::SameLine(statusRight - countWidth);
        ImGui::TextDisabled("%s", count.c_str());
    }

    void ProjectBrowser::DrawList(const std::vector<int>& visible, const ImVec2& size)
    {
        const float fontSize = ImGui::GetFontSize();
        const ImGuiStyle& style = ImGui::GetStyle();
        const float thumbHeight = fontSize * 2.6f;
        const float thumbWidth = thumbHeight * 16.0f / 9.0f;
        const float textHeight = ImGui::GetTextLineHeight() * 2.0f + style.ItemSpacing.y;
        const float rowHeight = std::max(thumbHeight, textHeight) + fontSize * 0.6f;

        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::kChild);
        ImGui::BeginChild("##List", size, ImGuiChildFlags_Borders);
        const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                      ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX;
        if (ImGui::BeginTable("##Projects", 3, flags)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("##Thumb", ImGuiTableColumnFlags_WidthFixed, thumbWidth);
            ImGui::TableSetupColumn("名前 / 場所", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("最終使用", ImGuiTableColumnFlags_WidthFixed, fontSize * 6.5f);
            ImGui::TableHeadersRow();

            for (const int index : visible) {
                const ProjectEntry& entry = entries_[index];
                ImGui::PushID(index);
                ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);

                // 行全体を選べるようにし、行の頭にサムネイルの代わりの面を描く
                ImGui::TableSetColumnIndex(0);
                const ImVec2 cellPos = ImGui::GetCursorScreenPos();
                const ImGuiSelectableFlags selectFlags = ImGuiSelectableFlags_SpanAllColumns |
                    ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap;
                if (ImGui::Selectable("##Row", index == selected_, selectFlags, ImVec2(0.0f, rowHeight))) {
                    selected_ = index;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        Request(Action::Open, entry.folder);
                    }
                }
                DrawThumbnail(ImGui::GetWindowDrawList(),
                    ImVec2(cellPos.x, cellPos.y + (rowHeight - thumbHeight) * 0.5f),
                    ImVec2(thumbWidth, thumbHeight), entry, ThumbnailOf(entry), 1.3f);

                // 名前と印、場所
                ImGui::TableSetColumnIndex(1);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (rowHeight - textHeight) * 0.5f);
                if (entry.missing) {
                    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.55f);
                }
                ImGui::TextUnformatted(entry.name.c_str());
                ImGui::SameLine();
                if (entry.bundled) {
                    ImGui::TextColored(Theme::kAccentHover, "リポジトリ内");
                } else {
                    ImGui::TextColored(Theme::kWarm, "外部");
                }
                if (IsCurrent(entry)) {
                    ImGui::SameLine();
                    ImGui::TextColored(Theme::kOk, "開いています");
                }
                if (entry.missing) {
                    ImGui::SameLine();
                    ImGui::TextColored(Theme::kWarn, "見つかりません");
                }
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextMute);
                ImGui::TextUnformatted(ToDisplay(entry.folder).c_str());
                ImGui::PopStyleColor();
                if (entry.missing) {
                    ImGui::PopStyleVar();
                }

                // 最後に開いた日時
                ImGui::TableSetColumnIndex(2);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f);
                ImGui::TextUnformatted(FormatLastOpened(entry.lastOpened).c_str());

                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (visible.empty()) {
            ImGui::TextDisabled("%s", entries_.empty()
                ? "プロジェクトがありません。「追加…」で既存のフォルダを選べます"
                : "一致するプロジェクトがありません");
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    void ProjectBrowser::DrawDetail(const ImVec2& size)
    {
        const float fontSize = ImGui::GetFontSize();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::kChild);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(fontSize * 0.8f, fontSize * 0.8f));
        ImGui::BeginChild("##Detail", size, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleVar();

        if (selected_ < 0 || selected_ >= static_cast<int>(entries_.size())) {
            ImGui::TextDisabled("左の一覧から選んでください");
            ImGui::EndChild();
            ImGui::PopStyleColor();
            return;
        }
        const ProjectEntry& entry = entries_[selected_];

        const float thumbWidth = ImGui::GetContentRegionAvail().x;
        const ImVec2 thumbSize(thumbWidth, thumbWidth * 9.0f / 16.0f);
        DrawThumbnail(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), thumbSize, entry, ThumbnailOf(entry), 3.0f);
        ImGui::Dummy(thumbSize);
        ImGui::Spacing();

        ImGui::TextUnformatted(entry.name.c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextMute);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(ToDisplay(entry.folder).c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::Spacing();

        if (entry.missing) {
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::kWarn);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted("フォルダが見つかりません。移したか消した可能性があります。");
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        } else if (ImGui::BeginTable("##Facts", 2)) {
            ImGui::TableSetupColumn("##Label", ImGuiTableColumnFlags_WidthFixed, fontSize * 5.5f);
            ImGui::TableSetupColumn("##Value", ImGuiTableColumnFlags_WidthStretch);
            const auto row = [](const char* label, const std::string& value) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(Theme::kTextMute, "%s", label);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(value.c_str());
                };
            row("起動シーン", entry.initialScene.empty() ? std::string("－") : entry.initialScene);
            row("シーン", std::to_string(entry.sceneCount));
            row("スクリプト", std::to_string(entry.scriptCount));
            row("最終使用", FormatLastOpened(entry.lastOpened));
            ImGui::EndTable();
        }
        ImGui::Spacing();

        const ImVec2 buttonSize(-FLT_MIN, 0.0f);
        const bool isCurrent = IsCurrent(entry);
        ImGui::BeginDisabled(entry.missing);
        ImGui::BeginDisabled(isCurrent);
        ImGui::PushStyleColor(ImGuiCol_Button, Theme::kAccent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::kAccentHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::kAccentMuted);
        if (ImGui::Button(isCurrent ? "開いています##Open" : "開く##Open", buttonSize)) {
            Request(Action::Open, entry.folder);
        }
        ImGui::PopStyleColor(3);
        ImGui::EndDisabled();
        if (ImGui::Button("エクスプローラーで表示", buttonSize)) {
            Request(Action::ShowInExplorer, entry.folder);
        }
        ImGui::EndDisabled();

        ImGui::BeginDisabled(entry.bundled);
        if (ImGui::Button("一覧から外す", buttonSize)) {
            Request(Action::Remove, entry.folder);
        }
        ImGui::EndDisabled();

        ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextMute);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(entry.bundled
            ? "リポジトリの中のプロジェクトは、開くたびに数え直すので一覧から外せません。"
            : "一覧から外しても、フォルダは消しません。");
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    void ProjectBrowser::DrawNewProject()
    {
        const float fontSize = ImGui::GetFontSize();
        const ImGuiStyle& style = ImGui::GetStyle();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(fontSize, fontSize));
        ImGui::BeginChild("##New", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleVar();

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("新しいプロジェクト");
        ImGui::Spacing();

        const float statusHeight = ImGui::GetTextLineHeightWithSpacing() + style.ItemSpacing.y;
        const ImVec2 body(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y - statusHeight);
        const float templatesWidth = fontSize * 17.0f;
        DrawTemplates(ImVec2(templatesWidth, body.y));
        ImGui::SameLine();
        DrawCreateForm(ImVec2(body.x - templatesWidth - style.ItemSpacing.x, body.y));

        DrawStatus();

        ImGui::EndChild();
    }

    void ProjectBrowser::DrawTemplates(const ImVec2& size)
    {
        const float fontSize = ImGui::GetFontSize();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::kChild);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(fontSize * 0.6f, fontSize * 0.6f));
        ImGui::BeginChild("##Templates", size, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleVar();

        ImGui::TextColored(Theme::kTextMute, "テンプレート");
        ImGui::Spacing();

        const float tileHeight = fontSize * 2.4f;
        const float tileWidth = tileHeight * 16.0f / 9.0f;
        const float rowHeight = tileHeight + fontSize * 0.6f;
        for (int i = 0; i < static_cast<int>(templates_.size()); ++i) {
            const ProjectTemplate& projectTemplate = templates_[i];
            ImGui::PushID(i);
            const ImVec2 rowPos = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable("##Template", i == selectedTemplate_, ImGuiSelectableFlags_None, ImVec2(0.0f, rowHeight))) {
                selectedTemplate_ = i;
            }
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 tilePos(rowPos.x, rowPos.y + (rowHeight - tileHeight) * 0.5f);
            DrawTile(drawList, tilePos, ImVec2(tileWidth, tileHeight), TileColor(projectTemplate.name),
                FirstLetter(projectTemplate.name), Theme::kText, false, 1.2f);

            const float textX = rowPos.x + tileWidth + fontSize * 0.6f;
            const float lineHeight = ImGui::GetTextLineHeight();
            const float textY = rowPos.y + (rowHeight - lineHeight * 2.0f) * 0.5f;
            drawList->AddText(ImVec2(textX, textY), ImGui::GetColorU32(Theme::kText), projectTemplate.name.c_str());
            const std::string summary = std::format("シーン {} ・ スクリプト {}",
                projectTemplate.scenes.size(), projectTemplate.scripts.size());
            drawList->AddText(ImVec2(textX, textY + lineHeight), ImGui::GetColorU32(Theme::kTextMute), summary.c_str());
            ImGui::PopID();
        }
        if (templates_.empty()) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("%s", "テンプレートがありません（Engine\\Templates\\Projects）");
            ImGui::PopTextWrapPos();
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    void ProjectBrowser::DrawCreateForm(const ImVec2& size)
    {
        const float fontSize = ImGui::GetFontSize();
        const ImGuiStyle& style = ImGui::GetStyle();
        ImGui::BeginChild("##Form", size, ImGuiChildFlags_None);

        if (templates_.empty()) {
            ImGui::EndChild();
            return;
        }
        const ProjectTemplate& projectTemplate = templates_[selectedTemplate_];

        // テンプレートの説明
        ImGui::TextUnformatted(projectTemplate.name.c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(projectTemplate.description.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 名前と保存先
        const float labelWidth = fontSize * 4.0f;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("名前");
        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputText("##Name", name_, sizeof(name_));

        const char* browseLabel = "参照…";
        const float browseWidth = ImGui::CalcTextSize(browseLabel).x + style.FramePadding.x * 2.0f;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("保存先");
        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(-(browseWidth + style.ItemSpacing.x));
        ImGui::InputText("##Location", location_, sizeof(location_));
        ImGui::SameLine();
        if (ImGui::Button(browseLabel)) {
            Request(Action::BrowseLocation, {});
        }

        // 作れるかどうか
        const std::string name = name_;
        const std::filesystem::path location = FromUtf8(location_);
        std::string warning;
        const std::string error = ProjectCreator::Check(name, location, &warning);
        const std::filesystem::path folder = location / FromUtf8(name);
        ImGui::PushTextWrapPos(0.0f);
        if (!error.empty()) {
            ImGui::TextColored(Theme::kError, "%s", error.c_str());
        } else if (!warning.empty()) {
            ImGui::TextColored(Theme::kWarn, "%s", warning.c_str());
        } else {
            ImGui::TextColored(Theme::kTextMute, "%s に作ります", ToDisplay(folder).c_str());
        }
        ImGui::PopTextWrapPos();
        ImGui::Spacing();

        // 作られるフォルダ
        ImGui::TextColored(Theme::kTextMute, "作られるフォルダ");
        const std::string shownName = name.empty() ? std::string("<名前>") : name;
        std::string scenes;
        for (const std::string& scene : projectTemplate.scenes) {
            scenes += (scenes.empty() ? "" : "・") + scene;
        }
        std::string scripts;
        for (const std::string& script : projectTemplate.scripts) {
            scripts += (scripts.empty() ? "" : "・") + script;
        }
        struct Line { std::string path; std::string note; };
        const Line lines[] = {
            { shownName + "\\", "" },
            { "├─ Application\\Assets\\Scenes\\", scenes },
            { "├─ Application\\Assets\\Scripts\\",
              (scripts.empty() ? std::string{} : scripts + "（") + "基底クラスなどは開いたときに書く" +
              (scripts.empty() ? "" : "）") },
            { "├─ Application\\Config\\EngineSettings\\Project.json",
              "名前 " + shownName + " ・ 起動シーン " + projectTemplate.initialScene },
            { "├─ .gitignore", "Application\\Saved・Intermediate・Build を git から外す" },
            { "└─ " + shownName + ".code-workspace", "VS Code でスクリプトを開く" },
        };
        // 説明の列は、説明のある行のうち一番長いパスの右に置く
        float pathWidth = 0.0f;
        float noteWidth = 0.0f;
        for (const Line& line : lines) {
            if (!line.note.empty()) {
                pathWidth = std::max(pathWidth, ImGui::CalcTextSize(line.path.c_str()).x);
                noteWidth = std::max(noteWidth, ImGui::CalcTextSize(line.note.c_str()).x);
            }
        }
        const ImVec2 treePadding(fontSize * 0.6f, fontSize * 0.5f);
        const float noteOffset = pathWidth + fontSize * 1.5f;
        // 横にはみ出すときは、横のスクロールバーの分だけ高くする
        const bool overflows = treePadding.x * 2.0f + noteOffset + noteWidth > ImGui::GetContentRegionAvail().x;
        const float treeHeight = ImGui::GetTextLineHeightWithSpacing() * static_cast<float>(std::size(lines)) +
                                 fontSize * 1.2f + (overflows ? style.ScrollbarSize : 0.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::kField);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, treePadding);
        ImGui::BeginChild("##Tree", ImVec2(0.0f, treeHeight),
            ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::PopStyleVar();
        const float noteX = ImGui::GetCursorPosX() + noteOffset;
        for (const Line& line : lines) {
            ImGui::TextUnformatted(line.path.c_str());
            if (!line.note.empty()) {
                ImGui::SameLine(noteX);
                ImGui::TextColored(Theme::kTextMute, "%s", line.note.c_str());
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::Spacing();

        // 右寄せのボタン
        const char* cancelLabel = "キャンセル";
        const char* createLabel = "作成して開く";
        const float buttonsWidth = ImGui::CalcTextSize(cancelLabel).x + ImGui::CalcTextSize(createLabel).x +
                                   style.FramePadding.x * 4.0f + style.ItemSpacing.x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - buttonsWidth));
        if (ImGui::Button(cancelLabel)) {
            ShowProjects();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!error.empty());
        ImGui::PushStyleColor(ImGuiCol_Button, Theme::kAccent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::kAccentHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::kAccentMuted);
        if (ImGui::Button(createLabel)) {
            Request(Action::Create, {});
        }
        ImGui::PopStyleColor(3);
        ImGui::EndDisabled();

        ImGui::EndChild();
    }

    void ProjectBrowser::ShowProjects()
    {
        view_ = View::Projects;
        SetStatus(kProjectsHint);
    }

    void ProjectBrowser::ShowNewProject()
    {
        view_ = View::NewProject;
        templates_ = ProjectCreator::ListTemplates();
        selectedTemplate_ = 0;

        // 保存先の既定は同梱プロジェクトのフォルダ。名前は空いている MyGame・MyGame2…
        const std::filesystem::path location = ProjectPaths::BundledProjectsDirectory();
        CopyToBuffer(location_, ToDisplay(location));
        std::error_code ec;
        std::string name = "MyGame";
        for (int number = 2; std::filesystem::exists(location / FromUtf8(name), ec) && number < 1000; ++number) {
            name = "MyGame" + std::to_string(number);
        }
        CopyToBuffer(name_, name);
        SetStatus(kNewProjectHint);
    }

    void ProjectBrowser::CreateProject()
    {
        if (templates_.empty()) {
            return;
        }
        std::filesystem::path folder;
        std::string error;
        if (!ProjectCreator::Create(templates_[selectedTemplate_], name_, FromUtf8(location_), &folder, &error)) {
            SetStatus(error, true);
            return;
        }
        // 作ったプロジェクトを一覧に足す
        list_.Add(folder);
        list_.Save();
        Open(folder);
    }

    void ProjectBrowser::Request(Action action, const std::filesystem::path& folder)
    {
        pending_ = action;
        pendingFolder_ = folder;
    }

    void ProjectBrowser::RunPendingAction()
    {
        const Action action = pending_;
        const std::filesystem::path folder = pendingFolder_;
        pending_ = Action::None;
        pendingFolder_.clear();

        switch (action) {
        case Action::Open:           Open(folder); break;
        case Action::ShowInExplorer: ShowInExplorer(folder); break;
        case Action::Remove:         Remove(folder); break;
        case Action::Add:            StartPicking(Action::Add, L"プロジェクトのフォルダを選ぶ"); break;
        case Action::Create:         CreateProject(); break;
        case Action::BrowseLocation: StartPicking(Action::BrowseLocation, L"新しいプロジェクトを作る場所を選ぶ"); break;
        default: break;
        }
    }

    void ProjectBrowser::Open(const std::filesystem::path& folder)
    {
        if (!current_.empty() && ProjectList::IsSameFolder(folder, current_)) {
            SetStatus("このプロジェクトはもう開いています");
            return;
        }
        if (!ProjectPaths::IsProjectFolder(folder)) {
            SetStatus(ToDisplay(folder.filename()) + " はプロジェクトのフォルダではありません", true);
            Refresh();
            return;
        }
        chosen_ = folder;
    }

    void ProjectBrowser::ShowInExplorer(const std::filesystem::path& folder)
    {
        const HINSTANCE result = ::ShellExecuteW(owner_, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(result) <= 32) {
            SetStatus(ToDisplay(folder.filename()) + " をエクスプローラーで開けませんでした", true);
        }
    }

    void ProjectBrowser::Remove(const std::filesystem::path& folder)
    {
        list_.Remove(folder);
        if (!list_.Save()) {
            SetStatus("一覧を保存できませんでした", true);
            return;
        }
        SetStatus(ToDisplay(folder.filename()) + " を一覧から外しました（フォルダはそのままです）");
        Refresh();
    }

    void ProjectBrowser::AddExisting(const std::filesystem::path& folder)
    {
        if (!list_.Add(folder)) {
            SetStatus(ToDisplay(folder.filename()) +
                " はプロジェクトのフォルダではありません（Application\\Config\\EngineSettings\\Project.json がありません）",
                true);
            return;
        }
        if (!list_.Save()) {
            SetStatus("一覧を保存できませんでした", true);
            return;
        }
        Refresh(folder);
        SetStatus(ProjectList::Inspect(folder).name + " を一覧に足しました");
    }

    void ProjectBrowser::StartPicking(Action purpose, const wchar_t* title)
    {
        if (picking_.valid()) {
            return;
        }
        pickingFor_ = purpose;
        picking_ = PickFolderAsync(owner_, title);
    }

    void ProjectBrowser::FinishPicking()
    {
        if (!picking_.valid() || picking_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            return;
        }
        const std::filesystem::path folder = picking_.get();
        const Action purpose = std::exchange(pickingFor_, Action::None);
        if (folder.empty()) {
            return;
        }
        if (purpose == Action::Add) {
            AddExisting(folder);
        } else if (purpose == Action::BrowseLocation) {
            CopyToBuffer(location_, ToDisplay(folder));
        }
    }
}

#endif // CORE_EDITOR
