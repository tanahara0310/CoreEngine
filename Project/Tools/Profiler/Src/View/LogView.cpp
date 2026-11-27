#include "View/LogView.h"

#include "Capture/FrameSnapshot.h"
#include "Capture/TracyAccess.h"
#include "Editor/ImGui/EditorTheme.h"
#include "Text/TextFormat.h"
#include "TracyWorker.hpp"
#include "View/DrawHelpers.h"

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <format>

namespace CoreEngine::Profiler
{
    namespace
    {
        namespace Theme = CoreEngine::Editor::Theme;

        /// @brief 表の列
        enum Column : int
        {
            kColumnTime,
            kColumnFrame,
            kColumnLevel,
            kColumnThread,
            kColumnText,
            kColumnNum,
        };

        LogLevel LevelOf(tracy::MessageSeverity severity)
        {
            switch (severity) {
            case tracy::MessageSeverity::Warning:
                return LogLevel::Warning;
            case tracy::MessageSeverity::Error:
            case tracy::MessageSeverity::Fatal:
                return LogLevel::Error;
            default:
                return LogLevel::Other;
            }
        }

        const char* LevelLabel(LogLevel level)
        {
            switch (level) {
            case LogLevel::Warning: return "警告";
            case LogLevel::Error: return "エラー";
            default: return "—";
            }
        }

        ImVec4 LevelColor(LogLevel level)
        {
            switch (level) {
            case LogLevel::Warning: return Theme::kWarn;
            case LogLevel::Error: return Theme::kError;
            default: return Theme::kTextMute;
            }
        }

        /// @brief 記録の始まりからの時刻を「分:秒.ミリ秒」にする（12345678901 → "0:12.345"）
        std::string FormatTime(int64_t nanoseconds)
        {
            const int64_t ms = std::max<int64_t>(0, nanoseconds / 1'000'000);
            return std::format("{}:{:02}.{:03}", ms / 60'000, (ms / 1000) % 60, ms % 1000);
        }

        /// @brief 行に出すスレッドの名前（メインスレッドはそう出し、名前の無いスレッドは番号で出す）
        std::string ThreadText(uint64_t id, uint64_t mainThreadId, const char* name)
        {
            if (id == mainThreadId) {
                return "メインスレッド";
            }
            if (name[0] == '\0' || std::strcmp(name, "???") == 0 || std::strcmp(name, "Main thread") == 0) {
                return std::format("スレッド {}", id);
            }
            return name;
        }

        /// @brief 改行を空白にして 1 行にする
        std::string SingleLine(const std::string& text)
        {
            std::string line = text;
            std::replace(line.begin(), line.end(), '\n', ' ');
            std::replace(line.begin(), line.end(), '\r', ' ');
            return line;
        }
    }

    void LogView::Reset()
    {
        entries_.clear();
        visible_.clear();
        lastCopied_ = nullptr;
        unnamed_.clear();
        mainThreadId_ = 0;
        firstTime_ = 0;
        warningCount_ = 0;
        errorCount_ = 0;
        otherCount_ = 0;
        needsVisible_ = true;
        hasSelection_ = false;
        selectedEntry_ = 0;
        shownCount_ = 0;
    }

    void LogView::Update(const tracy::Worker& worker, const FrameSnapshot& frames)
    {
        const auto& messages = worker.GetMessages();
        const size_t count = messages.size();
        firstTime_ = worker.GetFirstTime();
        if (count != entries_.size()) {
            size_t first = entries_.size();
            if (first > count || (first > 0 && &Access::At(messages, first - 1) != lastCopied_)) {
                // 時刻の順を保つために途中へ差し込まれた文があるので、全部写し直す
                entries_.clear();
                unnamed_.clear();
                warningCount_ = 0;
                errorCount_ = 0;
                otherCount_ = 0;
                hasSelection_ = false;
                first = 0;
            }
            CopyFrom(worker, first);
            lastCopied_ = count > 0 ? &Access::At(messages, count - 1) : nullptr;
            needsVisible_ = true;
        }

        // メインスレッドが見つかったら、全部の行のスレッドの名前を付け直す
        const tracy::ThreadData* mainThread = frames.MainThread();
        const uint64_t mainThreadId = mainThread ? mainThread->id : 0;
        if (mainThreadId != mainThreadId_) {
            mainThreadId_ = mainThreadId;
            unnamed_.resize(entries_.size());
            for (size_t i = 0; i < entries_.size(); ++i) {
                unnamed_[i] = i;
            }
        }
        NameThreads(worker);
    }

    void LogView::NameThreads(const tracy::Worker& worker)
    {
        if (unnamed_.empty()) {
            return;
        }
        std::erase_if(unnamed_, [&](size_t index) {
            LogEntry& entry = entries_[index];
            const char* name = worker.GetThreadName(entry.threadId);
            std::string text = ThreadText(entry.threadId, mainThreadId_, name);
            if (text != entry.thread) {
                entry.thread = std::move(text);
                needsVisible_ = true;
            }
            return entry.threadId == mainThreadId_ || std::strcmp(name, "???") != 0;
        });
    }

    void LogView::CopyFrom(const tracy::Worker& worker, size_t first)
    {
        const auto& messages = worker.GetMessages();
        entries_.reserve(messages.size());
        for (size_t i = first; i < messages.size(); ++i) {
            const tracy::MessageData& message = Access::At(messages, i);
            LogEntry entry;
            entry.time = message.time;
            entry.level = LevelOf(message.severity);
            entry.fromTracy = message.source == tracy::MessageSourceType::Tracy;
            entry.threadId = worker.DecompressThread(message.thread);
            entry.text = worker.GetString(message.ref);
            switch (entry.level) {
            case LogLevel::Warning: ++warningCount_; break;
            case LogLevel::Error: ++errorCount_; break;
            default: ++otherCount_; break;
            }
            unnamed_.push_back(entries_.size());
            entries_.push_back(std::move(entry));
        }
    }

    std::string LogView::TabLabel() const
    {
        if (entries_.empty()) {
            return "ログ###log";
        }
        return std::format("ログ（{}）###log", Text::FormatCount(entries_.size()));
    }

    bool LogView::Matches(const LogEntry& entry) const
    {
        if (levelFilter_ >= 0 && static_cast<int>(entry.level) != levelFilter_) {
            return false;
        }
        if (textFilter_[0] != '\0') {
            return Text::ContainsIgnoreCase(entry.text, textFilter_) || Text::ContainsIgnoreCase(entry.thread, textFilter_);
        }
        return true;
    }

    void LogView::RebuildVisible()
    {
        visible_.clear();
        for (size_t i = 0; i < entries_.size(); ++i) {
            if (Matches(entries_[i])) {
                visible_.push_back(i);
            }
        }
        needsVisible_ = false;
    }

    void LogView::Draw(const FrameSnapshot& frames, ViewState& view, bool live)
    {
        DrawToolbar();
        if (needsVisible_) {
            RebuildVisible();
        }
        DrawTable(frames, view, live);
    }

    void LogView::DrawToolbar()
    {
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
        if (entries_.empty()) {
            ImGui::TextUnformatted("エンジンの警告とエラーはまだありません。ログの警告とエラーは、つないでいる間だけここへ届きます。");
        } else if (otherCount_ > 0) {
            ImGui::Text("警告 %s・エラー %s・重さの無い文 %s。",
                Text::FormatCount(warningCount_).c_str(), Text::FormatCount(errorCount_).c_str(), Text::FormatCount(otherCount_).c_str());
        } else {
            ImGui::Text("警告 %s・エラー %s。", Text::FormatCount(warningCount_).c_str(), Text::FormatCount(errorCount_).c_str());
        }
        ImGui::PopStyleColor();
        ImGui::Spacing();

        const auto levelButton = [this](const char* label, int level) {
            if (Draw::ToggleButton(label, levelFilter_ == level)) {
                levelFilter_ = (level >= 0 && levelFilter_ == level) ? -1 : level;
                needsVisible_ = true;
            }
        };
        levelButton("すべて", -1);
        ImGui::SameLine(0.0f, 4.0f);
        levelButton("警告", static_cast<int>(LogLevel::Warning));
        ImGui::SameLine(0.0f, 4.0f);
        levelButton("エラー", static_cast<int>(LogLevel::Error));
        if (otherCount_ > 0) {
            ImGui::SameLine(0.0f, 4.0f);
            levelButton("重さの無い文", static_cast<int>(LogLevel::Other));
        }

        ImGui::SameLine(0.0f, 18.0f);
        ImGui::SetNextItemWidth(std::max(160.0f, ImGui::GetContentRegionAvail().x));
        if (ImGui::InputTextWithHint("##logFilter", "文かスレッドで絞り込む", textFilter_, sizeof(textFilter_))) {
            needsVisible_ = true;
        }
    }

    void LogView::DrawTable(const FrameSnapshot& frames, ViewState& view, bool live)
    {
        if (entries_.empty()) {
            return;
        }
        const ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable | ImGuiTableFlags_RowBg
            | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
        const float footerHeight = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
        if (!ImGui::BeginTable("##log", kColumnNum, flags, ImVec2(0.0f, -footerHeight))) {
            return;
        }
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("時刻", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("00:00.000").x, kColumnTime);
        ImGui::TableSetupColumn("フレーム", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("フレーム 0").x, kColumnFrame);
        ImGui::TableSetupColumn("重さ", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("● エラー").x, kColumnLevel);
        ImGui::TableSetupColumn("スレッド", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("スレッド 000000").x, kColumnThread);
        ImGui::TableSetupColumn("文", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoHide, 0.0f, kColumnText);
        ImGui::TableHeadersRow();

        const bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;
        const bool hasFrames = frames.Count() > 0;

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(visible_.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const size_t index = visible_[static_cast<size_t>(i)];
                const LogEntry& entry = entries_[index];
                const size_t frame = hasFrames ? frames.FindFrame(entry.time) : 0;
                ImGui::TableNextRow();
                ImGui::PushID(i);

                ImGui::TableSetColumnIndex(kColumnTime);
                const bool selected = hasSelection_ && selectedEntry_ == index;
                if (ImGui::Selectable("##row", selected,
                    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_AllowDoubleClick)) {
                    hasSelection_ = true;
                    selectedEntry_ = index;
                    if (hasFrames) {
                        view.followLatest = false;
                        SelectFrame(view, frames, frame);
                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                            view.tab = ViewTab::Timeline;
                            view.tabRequested = true;
                        }
                    }
                }
                if (ImGui::BeginItemTooltip()) {
                    ImGui::PushTextWrapPos(ImGui::GetTextLineHeight() * 40.0f);
                    ImGui::PushStyleColor(ImGuiCol_Text, LevelColor(entry.level));
                    ImGui::TextUnformatted(LevelLabel(entry.level));
                    ImGui::PopStyleColor();
                    ImGui::TextUnformatted(entry.text.c_str());
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
                    if (hasFrames) {
                        ImGui::Text("%s・フレーム %s・%s%s", FormatTime(entry.time - firstTime_).c_str(),
                            Text::FormatCount(frames.Number(frame)).c_str(), entry.thread.c_str(), entry.fromTracy ? "・Tracy から" : "");
                    } else {
                        ImGui::Text("%s・%s", FormatTime(entry.time - firstTime_).c_str(), entry.thread.c_str());
                    }
                    ImGui::TextUnformatted("クリックでそのフレームを選び、ダブルクリックでタイムラインを開きます。");
                    ImGui::PopStyleColor();
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
                if (ImGui::BeginPopupContextItem("##rowMenu")) {
                    if (ImGui::MenuItem("文をコピー")) {
                        ImGui::SetClipboardText(entry.text.c_str());
                    }
                    ImGui::EndPopup();
                }
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
                ImGui::TextUnformatted(FormatTime(entry.time - firstTime_).c_str());
                ImGui::PopStyleColor();

                ImGui::TableSetColumnIndex(kColumnFrame);
                if (hasFrames) {
                    Draw::TextRight(Text::FormatCount(frames.Number(frame)).c_str(), Theme::kTextDim);
                }

                ImGui::TableSetColumnIndex(kColumnLevel);
                ImGui::PushStyleColor(ImGuiCol_Text, LevelColor(entry.level));
                ImGui::TextUnformatted(entry.level == LogLevel::Other ? LevelLabel(entry.level)
                    : (entry.level == LogLevel::Error ? "● エラー" : "● 警告"));
                ImGui::PopStyleColor();

                ImGui::TableSetColumnIndex(kColumnThread);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
                ImGui::TextUnformatted(entry.thread.c_str());
                ImGui::PopStyleColor();

                ImGui::TableSetColumnIndex(kColumnText);
                ImGui::TextUnformatted(SingleLine(entry.text).c_str());

                ImGui::PopID();
            }
        }
        if (live && atBottom && visible_.size() > shownCount_) {
            ImGui::SetScrollHereY(1.0f);
        }
        shownCount_ = visible_.size();
        ImGui::EndTable();

        ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextMute);
        if (visible_.empty()) {
            ImGui::TextUnformatted("当てはまる文がありません。");
        } else {
            ImGui::Text("%s / %s 行を表示。クリックでそのフレームを選び、ダブルクリックでタイムラインを開きます。右クリックで文をコピーできます。",
                Text::FormatCount(visible_.size()).c_str(), Text::FormatCount(entries_.size()).c_str());
        }
        ImGui::PopStyleColor();
    }
}
