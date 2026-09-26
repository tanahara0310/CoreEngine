#include "pch.h"
#include "Editor/Launcher/ProjectLauncher.h"

#ifdef CORE_EDITOR

#include "Editor/ImGui/EditorTheme.h"
#include "Editor/ImGui/ImGuiManager.h"
#include "Editor/Launcher/ProjectList.h"
#include "EngineSystem/EngineConfig.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/GraphicsCoreDesc.h"
#include "Graphics/RHI/SwapChain/SwapChain.h"
#include "Utility/Path/ProjectPaths.h"
#include "WinApp/WinApp.h"

#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

#include <Windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cstdio>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace CoreEngine::Editor
{
    namespace
    {
        namespace Theme = Editor::Theme;

        /// 画面を出させる起動の引数
        constexpr std::wstring_view kLauncherOption = L"--launcher";

        /// @brief 起動の引数に `--launcher` があるか
        bool HasLauncherOption()
        {
            int count = 0;
            LPWSTR* const args = ::CommandLineToArgvW(::GetCommandLineW(), &count);
            if (!args) {
                return false;
            }
            bool found = false;
            for (int i = 1; i < count; ++i) {
                if (std::wstring_view(args[i]) == kLauncherOption) {
                    found = true;
                    break;
                }
            }
            ::LocalFree(args);
            return found;
        }

        /// @brief path を表示用の UTF-8 にする（区切りは Windows の '\'）
        std::string ToDisplay(const std::filesystem::path& path)
        {
            const std::u8string text = path.u8string();
            return std::string(text.begin(), text.end());
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
        ImVec4 ThumbnailColor(const ProjectEntry& entry)
        {
            if (entry.missing) {
                return Theme::kField;
            }
            static const ImVec4 kPalette[] = {
                Theme::FromSrgb(52, 84, 122),
                Theme::FromSrgb(84, 64, 120),
                Theme::FromSrgb(40, 98, 88),
                Theme::FromSrgb(122, 80, 44),
                Theme::FromSrgb(104, 50, 64),
                Theme::FromSrgb(62, 74, 96),
            };
            size_t hash = 0;
            for (const char c : entry.name) {
                hash = hash * 31 + static_cast<unsigned char>(c);
            }
            return kPalette[hash % std::size(kPalette)];
        }

        /// @brief サムネイルの代わりに、色の面と名前の頭文字を描く
        void DrawThumbnail(ImDrawList* drawList, const ImVec2& min, const ImVec2& size,
                           const ProjectEntry& entry, float letterScale)
        {
            const ImVec2 max(min.x + size.x, min.y + size.y);
            drawList->AddRectFilled(min, max, ImGui::GetColorU32(ThumbnailColor(entry)), 3.0f);
            if (entry.missing) {
                drawList->AddRect(min, max, ImGui::GetColorU32(Theme::kOutline), 3.0f);
            }

            const std::string letter = entry.missing ? std::string("?") : FirstLetter(entry.name);
            const float fontSize = ImGui::GetFontSize() * letterScale;
            const ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, letter.c_str());
            const ImVec2 textPos(min.x + (size.x - textSize.x) * 0.5f, min.y + (size.y - textSize.y) * 0.5f);
            const ImVec4 textColor = entry.missing ? Theme::kTextMute : Theme::kText;
            drawList->AddText(ImGui::GetFont(), fontSize, textPos, ImGui::GetColorU32(textColor), letter.c_str());
        }

        /// @brief フォルダを選ぶ窓を出す（選ばれなければ空）
        std::filesystem::path PickFolder(HWND owner)
        {
            Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
            if (FAILED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
                return {};
            }
            DWORD options = 0;
            dialog->GetOptions(&options);
            dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
            dialog->SetTitle(L"プロジェクトのフォルダを選ぶ");
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

        /// @brief プロジェクトを選ぶ画面の中身
        class LauncherScreen
        {
        public:
            LauncherScreen(ProjectList& list, HWND hwnd)
                : list_(list), hwnd_(hwnd), engineRootText_(ToDisplay(ProjectPaths::EngineRoot()))
            {
                Refresh();
            }

            /// @brief 1 フレーム分を描き、押された操作を行う
            void Draw();

            /// @brief 開くプロジェクトが決まったか
            bool IsDone() const { return done_; }

        private:
            /// @brief 押された操作（描き終えてから行う）
            enum class Action { None, Open, ShowInExplorer, Remove, Add };

            void Refresh(const std::filesystem::path& select = {});
            void SetStatus(std::string text, bool error = false);
            std::vector<int> VisibleIndices() const;

            void DrawNav();
            void DrawMain();
            void DrawList(const std::vector<int>& visible, const ImVec2& size);
            void DrawDetail(const ImVec2& size);
            void Request(Action action, const std::filesystem::path& folder);
            void RunPendingAction();

            void Open(const std::filesystem::path& folder);
            void ShowInExplorer(const std::filesystem::path& folder);
            void Remove(const std::filesystem::path& folder);
            void AddExisting();

            ProjectList& list_;
            HWND hwnd_ = nullptr;
            std::string engineRootText_;
            std::vector<ProjectEntry> entries_;
            int selected_ = -1;
            char filter_[128] = {};
            std::string status_ = "プロジェクトを選んで「開く」を押してください（行のダブルクリックでも開けます）";
            bool statusError_ = false;
            Action pending_ = Action::None;
            std::filesystem::path pendingFolder_;
            bool done_ = false;
        };

        void LauncherScreen::Refresh(const std::filesystem::path& select)
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

        void LauncherScreen::SetStatus(std::string text, bool error)
        {
            status_ = std::move(text);
            statusError_ = error;
        }

        std::vector<int> LauncherScreen::VisibleIndices() const
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

        void LauncherScreen::Draw()
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::kDeepest);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::Begin("##Launcher", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoBringToFrontOnFocus);
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();

            // 中央に、フォントの大きさを基準にした枠を置く
            const float fontSize = ImGui::GetFontSize();
            const ImVec2 area = ImGui::GetContentRegionAvail();
            const ImVec2 size(std::min(area.x - fontSize * 2.0f, fontSize * 92.0f),
                              std::min(area.y - fontSize * 2.0f, fontSize * 56.0f));
            ImGui::SetCursorPos(ImVec2((area.x - size.x) * 0.5f, (area.y - size.y) * 0.5f));

            ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::kWindow);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::BeginChild("##Panel", size, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
            ImGui::PopStyleVar();
            DrawNav();
            ImGui::SameLine(0.0f, 0.0f);
            DrawMain();
            ImGui::EndChild();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();

            // 一覧で選んだものを Enter で開く
            if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Enter, false) &&
                selected_ >= 0 && selected_ < static_cast<int>(entries_.size())) {
                Request(Action::Open, entries_[selected_].folder);
            }

            ImGui::End();

            RunPendingAction();
        }

        void LauncherScreen::DrawNav()
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

            ImGui::Selectable("プロジェクト", true);

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

        void LauncherScreen::DrawMain()
        {
            const float fontSize = ImGui::GetFontSize();
            const ImGuiStyle& style = ImGui::GetStyle();
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(fontSize, fontSize));
            ImGui::BeginChild("##Main", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
            ImGui::PopStyleVar();

            // 見出しと、絞り込み・追加
            const float headerRight = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("プロジェクト");
            const float filterWidth = fontSize * 14.0f;
            const char* addLabel = "追加…";
            const float addWidth = ImGui::CalcTextSize(addLabel).x + style.FramePadding.x * 2.0f;
            ImGui::SameLine(headerRight - filterWidth - addWidth - style.ItemSpacing.x);
            ImGui::SetNextItemWidth(filterWidth);
            ImGui::InputTextWithHint("##Filter", "名前・場所で絞り込む", filter_, sizeof(filter_));
            ImGui::SameLine();
            if (ImGui::Button(addLabel)) {
                Request(Action::Add, {});
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("ほかの場所にあるプロジェクトのフォルダを選んで、一覧に足します");
            }
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

            // 状態の一言（件数に重なる分は切る）と件数
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

            ImGui::EndChild();
        }

        void LauncherScreen::DrawList(const std::vector<int>& visible, const ImVec2& size)
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
                        ImVec2(thumbWidth, thumbHeight), entry, 1.3f);

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

        void LauncherScreen::DrawDetail(const ImVec2& size)
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
            DrawThumbnail(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), thumbSize, entry, 3.0f);
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
            ImGui::BeginDisabled(entry.missing);
            ImGui::PushStyleColor(ImGuiCol_Button, Theme::kAccent);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::kAccentHover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::kAccentMuted);
            if (ImGui::Button("開く", buttonSize)) {
                Request(Action::Open, entry.folder);
            }
            ImGui::PopStyleColor(3);
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

        void LauncherScreen::Request(Action action, const std::filesystem::path& folder)
        {
            pending_ = action;
            pendingFolder_ = folder;
        }

        void LauncherScreen::RunPendingAction()
        {
            const Action action = pending_;
            const std::filesystem::path folder = pendingFolder_;
            pending_ = Action::None;
            pendingFolder_.clear();

            switch (action) {
            case Action::Open:           Open(folder); break;
            case Action::ShowInExplorer: ShowInExplorer(folder); break;
            case Action::Remove:         Remove(folder); break;
            case Action::Add:            AddExisting(); break;
            default: break;
            }
        }

        void LauncherScreen::Open(const std::filesystem::path& folder)
        {
            if (!ProjectPaths::OpenProject(folder)) {
                SetStatus(ToDisplay(folder.filename()) + " はプロジェクトのフォルダではありません", true);
                Refresh();
                return;
            }
            list_.MarkOpened(folder);
            list_.Save();
            done_ = true;
        }

        void LauncherScreen::ShowInExplorer(const std::filesystem::path& folder)
        {
            const HINSTANCE result = ::ShellExecuteW(hwnd_, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            if (reinterpret_cast<INT_PTR>(result) <= 32) {
                SetStatus(ToDisplay(folder.filename()) + " をエクスプローラーで開けませんでした", true);
            }
        }

        void LauncherScreen::Remove(const std::filesystem::path& folder)
        {
            list_.Remove(folder);
            if (!list_.Save()) {
                SetStatus("一覧を保存できませんでした", true);
                return;
            }
            SetStatus(ToDisplay(folder.filename()) + " を一覧から外しました（フォルダはそのままです）");
            Refresh();
        }

        void LauncherScreen::AddExisting()
        {
            const std::filesystem::path folder = PickFolder(hwnd_);
            if (folder.empty()) {
                return;
            }
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
    }

    bool ProjectLauncher::Run(WinApp& winApp, const EngineConfig& config)
    {
        ProjectList list;
        list.Load();

        // 前回のプロジェクトを自動で開く
        if (!HasLauncherOption() && list.GetOpenLastOnStartup()) {
            const std::filesystem::path last = list.LastOpened();
            if (!last.empty() && ProjectPaths::OpenProject(last)) {
                list.MarkOpened(last);
                list.Save();
                return true;
            }
        }

        // フォルダを選ぶ窓が COM を使う。エンジンは後で別の方式で初期化するので、戻る前に閉じる
        const HRESULT comResult = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

        const HWND hwnd = winApp.GetHwnd();
        GraphicsCore graphics;
        GraphicsCoreDesc desc{};
        desc.hwnd = hwnd;
        desc.clientWidth = winApp.GetClientWidth();
        desc.clientHeight = winApp.GetClientHeight();
        desc.enableDebugLayer = config.enableDebugLayer;
        desc.enableGPUBasedValidation = config.enableGPUBasedValidation;
        desc.framesInFlight = 2;
        desc.maxSRVDescriptors = 64;
        desc.maxRTVDescriptors = 16;
        desc.maxDSVDescriptors = 1;
        graphics.Initialize(desc);
        winApp.SetResizeCallback([&graphics](int32_t width, int32_t height) {
            graphics.OnWindowResize(width, height);
            });

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::StyleColorsDark();
        ImGuiManager::ApplyCustomTheme();
        ImGuiManager::LoadFonts(ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd));
        ImGui_ImplWin32_Init(hwnd);

        SwapChain& swapChain = graphics.GetSwapChain();
        DescriptorHandle fontDescriptor = graphics.GetDescriptorAllocator()->AllocateSRVHandle("LauncherFont");
        ImGui_ImplDX12_Init(graphics.GetDevice(), static_cast<int>(swapChain.BufferCount()), swapChain.RTVFormat(),
            graphics.GetSRVHeap(), fontDescriptor.cpuHandle, fontDescriptor.gpuHandle);
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(nullptr, nullptr, nullptr);
        ImGui_ImplDX12_CreateDeviceObjects();

        ::SetWindowTextW(hwnd, L"CoreEngine — プロジェクトを開く");
        ::ShowWindow(hwnd, SW_SHOW);
        ::SetForegroundWindow(hwnd);

        LauncherScreen screen(list, hwnd);
        bool closed = false;
        while (!screen.IsDone()) {
            if (winApp.ProcessMessage()) {
                closed = true;
                break;
            }
            if (::IsIconic(hwnd)) {
                ::Sleep(16);
                continue;
            }

            ImGui_ImplDX12_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            screen.Draw();
            ImGui::Render();

            // バックバッファを描画先にして、塗りつぶしてから ImGui を描き、表示へ戻す
            const FrameContext frame = graphics.BeginFrame();
            const uint32_t backBufferIndex = swapChain.CurrentBackBufferIndex();
            Barrier::Transition(frame.cmdList, swapChain.BackBuffer(backBufferIndex), D3D12_RESOURCE_STATE_RENDER_TARGET);
            const D3D12_CPU_DESCRIPTOR_HANDLE rtv = swapChain.RTV(backBufferIndex);
            frame.cmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
            const float clearColor[4] = { Theme::kDeepest.x, Theme::kDeepest.y, Theme::kDeepest.z, 1.0f };
            frame.cmdList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), frame.cmdList);
            Barrier::Transition(frame.cmdList, swapChain.BackBuffer(backBufferIndex), D3D12_RESOURCE_STATE_PRESENT);
            graphics.EndFrame(1);
        }

        graphics.WaitForGpuIdle();
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        winApp.SetResizeCallback(nullptr);
        graphics.Shutdown();

        // エンジンの初期化が済むまで窓を隠す（起動の間はスプラッシュが出る）
        ::ShowWindow(hwnd, SW_HIDE);
        ::SetWindowTextW(hwnd, config.GetWindowTitleWide().c_str());

        if (SUCCEEDED(comResult)) {
            ::CoUninitialize();
        }
        return !closed;
    }
}

#endif // CORE_EDITOR
