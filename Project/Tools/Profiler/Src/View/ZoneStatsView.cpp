#include "View/ZoneStatsView.h"

#include "Editor/ImGui/EditorTheme.h"
#include "Capture/FrameSnapshot.h"
#include "Capture/TracyAccess.h"
#include "Text/TextFormat.h"
#include "Text/ZoneDescriptions.h"
#include "TracyWorker.hpp"
#include "View/DrawHelpers.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>

namespace CoreEngine::Profiler
{
    namespace
    {
        namespace Theme = CoreEngine::Editor::Theme;

        /// @brief 受け取りの最中に統計を写し直す間隔（秒）
        constexpr double kLiveCopyInterval = 0.5;

        /// @brief 表の列
        enum Column : int
        {
            kColumnName,
            kColumnKind,
            kColumnCount,
            kColumnTotal,
            kColumnMean,
            kColumnPerFrame,
            kColumnMax,
            kColumnStddev,
            kColumnLocation,
            kColumnNum,
        };

        /// @brief 種類の色の小さな四角を、今の行の文字の高さの真ん中に描いて、その分だけ右へ進める
        void KindSwatch(ZoneKind kind)
        {
            const float size = ImGui::GetTextLineHeight() * 0.55f;
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const float top = pos.y + (ImGui::GetTextLineHeight() - size) * 0.5f;
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x, top), ImVec2(pos.x + size, top + size),
                ImGui::GetColorU32(ZoneKindColor(kind)), 1.5f);
            ImGui::Dummy(ImVec2(size, ImGui::GetTextLineHeight()));
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        }

        /// @brief セルの右端に寄せて文字を出す
        void TextRight(const std::string& text)
        {
            const float width = ImGui::CalcTextSize(text.c_str()).x;
            const float space = ImGui::GetContentRegionAvail().x - width;
            if (space > 0.0f) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + space);
            }
            ImGui::TextUnformatted(text.c_str());
        }

        /// @brief 分布の横の 10 の累乗 1 つあたりの棒の数
        constexpr int kBinsPerDecade = 10;

        std::string Ms(double nanoseconds)
        {
            return Text::FormatMs(nanoseconds) + " ms";
        }

        /// @brief 分布の目盛りの字（decade はナノ秒の 10 の累乗の指数）
        std::string DecadeLabel(int decade)
        {
            if (decade >= 6) {
                uint64_t ms = 1;
                for (int i = 6; i < decade; ++i) {
                    ms *= 10;
                }
                return Text::FormatCount(ms) + " ms";
            }
            return std::format("{:.{}f} ms", std::pow(10.0, decade - 6), 6 - decade);
        }

        /// @brief 目標と比べた色（目標の中は描画の青）
        ImVec4 LevelColor(int64_t duration, int64_t target)
        {
            switch (FrameLevelOf(duration, target)) {
            case FrameLevel::Far: return Theme::kError;
            case FrameLevel::Over: return Theme::kWarm;
            default: return Theme::kAccentHover;
            }
        }
    }

    void ZoneStatsView::Reset()
    {
        cpuRows_.clear();
        gpuRows_.clear();
        visible_.clear();
        cpuCopied_ = false;
        gpuCopied_ = false;
        wasLive_ = false;
        waitingForStatistics_ = false;
        lastCopyTime_ = -1.0;
        frameCount_ = 0;
        needsSort_ = true;
        needsVisible_ = true;
        hasSelection_ = false;
        distribution_ = Distribution{};
        lastDistribution_ = -1.0;
    }

    void ZoneStatsView::Update(const tracy::Worker& worker, bool live, double now)
    {
        const bool cpuReady = worker.AreSourceLocationZonesReady();
        const bool gpuReady = worker.AreGpuSourceLocationZonesReady();
        waitingForStatistics_ = !cpuReady;

        bool copy = false;
        if (live) {
            copy = lastCopyTime_ < 0.0 || now - lastCopyTime_ >= kLiveCopyInterval;
        } else {
            copy = wasLive_ || (cpuReady && !cpuCopied_) || (gpuReady && !gpuCopied_);
        }
        wasLive_ = live;
        if (!copy) {
            return;
        }
        CopyRows(worker, cpuReady, gpuReady);
        lastCopyTime_ = now;
    }

    void ZoneStatsView::CopyRows(const tracy::Worker& worker, bool copyCpu, bool copyGpu)
    {
        const tracy::FrameData* frames = worker.GetFramesBase();
        frameCount_ = frames ? worker.GetFrameCount(*frames) : 0;
        const double frameDivisor = frameCount_ > 0 ? static_cast<double>(frameCount_) : 1.0;

        const auto makeRow = [&](int16_t srcloc, uint64_t count, int64_t total, int64_t max, double sumSq) {
            const tracy::SourceLocation& location = worker.GetSourceLocation(srcloc);
            ZoneStatRow row;
            row.srcloc = srcloc;
            row.name = worker.GetString(location.name.active ? location.name : location.function);
            row.file = worker.GetString(location.file);
            row.line = location.line;
            row.location = std::format("{}:{}", Text::FileName(row.file), row.line);
            row.kind = ClassifyZone(row.name, row.file);
            row.count = count;
            row.total = total;
            row.max = max;
            const double n = static_cast<double>(count);
            row.mean = static_cast<double>(total) / n;
            row.perFrame = static_cast<double>(total) / frameDivisor;
            if (count > 1) {
                const double squares = sumSq - 2.0 * static_cast<double>(total) * row.mean + row.mean * row.mean * n;
                row.stddev = std::sqrt(std::max(0.0, squares / (n - 1.0)));
            }
            return row;
        };

        if (copyCpu) {
            cpuRows_.clear();
            for (const auto& [srcloc, zones] : worker.GetSourceLocationZones()) {
                if (zones.total == 0 || zones.zones.empty()) {
                    continue;
                }
                cpuRows_.push_back(makeRow(srcloc, zones.zones.size(), zones.total, zones.max, zones.sumSq));
            }
            cpuCopied_ = true;
        }
        if (copyGpu) {
            gpuRows_.clear();
            for (const auto& [srcloc, zones] : worker.GetGpuSourceLocationZones()) {
                if (zones.total == 0 || zones.zones.empty()) {
                    continue;
                }
                ZoneStatRow row = makeRow(srcloc, zones.zones.size(), zones.total, zones.max, zones.sumSq);
                row.kind = ZoneKind::Render;
                gpuRows_.push_back(std::move(row));
            }
            gpuCopied_ = true;
        }
        needsSort_ = true;
    }

    void ZoneStatsView::SortRows(std::vector<ZoneStatRow>& rows) const
    {
        const auto key = [this](const ZoneStatRow& a, const ZoneStatRow& b) -> int {
            const auto compare = [](auto x, auto y) { return (x < y) ? -1 : (y < x ? 1 : 0); };
            switch (sortColumn_) {
            case kColumnName: return a.name.compare(b.name);
            case kColumnKind: return compare(static_cast<int>(a.kind), static_cast<int>(b.kind));
            case kColumnCount: return compare(a.count, b.count);
            case kColumnTotal: return compare(a.total, b.total);
            case kColumnMean: return compare(a.mean, b.mean);
            case kColumnPerFrame: return compare(a.perFrame, b.perFrame);
            case kColumnMax: return compare(a.max, b.max);
            case kColumnStddev: return compare(a.stddev, b.stddev);
            case kColumnLocation: return a.location.compare(b.location);
            default: return 0;
            }
        };
        std::stable_sort(rows.begin(), rows.end(), [&](const ZoneStatRow& a, const ZoneStatRow& b) {
            int order = key(a, b);
            if (order == 0) {
                order = (a.total < b.total) ? 1 : (b.total < a.total ? -1 : a.name.compare(b.name));
                return order < 0;
            }
            return sortAscending_ ? order < 0 : order > 0;
        });
    }

    void ZoneStatsView::RebuildVisible()
    {
        visible_.clear();
        const std::vector<ZoneStatRow>& rows = showGpu_ ? gpuRows_ : cpuRows_;
        for (const ZoneStatRow& row : rows) {
            if (!showGpu_ && kindFilter_ >= 0 && static_cast<int>(row.kind) != kindFilter_) {
                continue;
            }
            if (nameFilter_[0] != '\0'
                && !Text::ContainsIgnoreCase(row.name, nameFilter_)
                && !Text::ContainsIgnoreCase(row.location, nameFilter_)) {
                continue;
            }
            visible_.push_back(&row);
        }
    }

    void ZoneStatsView::UpdateSelection(const tracy::Worker& worker, const FrameSnapshot& frames, const ViewState& view, bool live, double now)
    {
        if (!hasSelection_) {
            distribution_.valid = false;
            return;
        }
        const bool changed = !distribution_.valid || distribution_.srcloc != selectionSrcloc_ || distribution_.gpu != selectionGpu_
            || distribution_.target != view.TargetNs();
        const bool refresh = live && now - lastDistribution_ >= kLiveCopyInterval;
        if (!changed && !refresh) {
            return;
        }
        const bool ready = selectionGpu_ ? worker.AreGpuSourceLocationZonesReady() : worker.AreSourceLocationZonesReady();
        if (!ready) {
            distribution_.valid = false;
            return;
        }
        lastDistribution_ = now;

        Distribution distribution;
        distribution.valid = true;
        distribution.gpu = selectionGpu_;
        distribution.srcloc = selectionSrcloc_;
        distribution.target = view.TargetNs();
        const tracy::SourceLocation& location = worker.GetSourceLocation(selectionSrcloc_);
        distribution.name = worker.GetString(location.name.active ? location.name : location.function);
        const std::string file = worker.GetString(location.file);
        distribution.description = ZoneDescriptions::Instance().Describe(distribution.name, file);
        distribution.location = std::format("{}:{}", Text::FileName(file), location.line);

        std::vector<int64_t> durations;
        int64_t longestStart = 0;
        if (!selectionGpu_) {
            const auto& map = worker.GetSourceLocationZones();
            if (const auto it = map.find(selectionSrcloc_); it != map.end()) {
                durations.reserve(it->second.zones.size());
                for (const auto& entry : it->second.zones) {
                    const tracy::ZoneEvent* event = entry.Zone();
                    if (!event->IsEndValid()) {
                        continue;
                    }
                    const int64_t time = event->End() - event->Start();
                    durations.push_back(time);
                    if (time > distribution.max || !distribution.maxRef.IsValid()) {
                        distribution.max = time;
                        distribution.maxRef = ZoneRef{ event, nullptr, worker.DecompressThread(entry.Thread()) };
                        longestStart = event->Start();
                    }
                }
            }
        } else {
            const auto& map = worker.GetGpuSourceLocationZones();
            const tracy::GpuEvent* longest = nullptr;
            if (const auto it = map.find(selectionSrcloc_); it != map.end()) {
                durations.reserve(it->second.zones.size());
                for (const auto& entry : it->second.zones) {
                    const tracy::GpuEvent* event = entry.Zone();
                    if (event->GpuStart() < 0 || event->GpuEnd() < 0) {
                        continue;
                    }
                    const int64_t time = event->GpuEnd() - event->GpuStart();
                    durations.push_back(time);
                    if (time > distribution.max || !longest) {
                        distribution.max = time;
                        longest = event;
                        longestStart = event->GpuStart();
                    }
                }
            }
            // いちばん長かった 1 回が載っている GPU のキューを探す
            const auto& contexts = worker.GetGpuData();
            for (size_t context = 0; longest && context < contexts.size() && !distribution.maxRef.IsValid(); ++context) {
                for (const auto& entry : contexts[context]->threadData) {
                    if (Access::FindGpu(worker, entry.second.timeline, selectionSrcloc_, longestStart, longestStart + 1) == longest) {
                        distribution.maxRef = ZoneRef{ nullptr, longest, context };
                        break;
                    }
                }
            }
        }

        distribution.count = durations.size();
        if (!durations.empty()) {
            distribution.min = *std::min_element(durations.begin(), durations.end());
            distribution.maxFrame = frames.FindFrame(longestStart);
            for (const int64_t time : durations) {
                if (FrameLevelOf(time, distribution.target) != FrameLevel::Normal) {
                    ++distribution.overTarget;
                }
            }
            const double low = std::log10(static_cast<double>(std::max<int64_t>(distribution.min, 1)));
            const double high = std::log10(static_cast<double>(std::max<int64_t>(distribution.max, 1)));
            distribution.firstDecade = static_cast<int>(std::floor(low));
            const int lastDecade = std::max(static_cast<int>(std::ceil(high)), distribution.firstDecade + 2);
            distribution.decades = lastDecade - distribution.firstDecade;
            distribution.bins.assign(static_cast<size_t>(distribution.decades * kBinsPerDecade), 0);
            for (const int64_t time : durations) {
                const double position = (std::log10(static_cast<double>(std::max<int64_t>(time, 1))) - distribution.firstDecade) * kBinsPerDecade;
                const int bin = std::clamp(static_cast<int>(std::floor(position)), 0, static_cast<int>(distribution.bins.size()) - 1);
                ++distribution.bins[static_cast<size_t>(bin)];
            }
            const auto middle = durations.begin() + static_cast<std::ptrdiff_t>(durations.size() / 2);
            std::nth_element(durations.begin(), middle, durations.end());
            distribution.median = *middle;
        }
        distribution_ = std::move(distribution);
    }

    void ZoneStatsView::Draw(const FrameSnapshot& frames, ViewState& view)
    {
        if (needsSort_) {
            SortRows(cpuRows_);
            SortRows(gpuRows_);
            needsSort_ = false;
            needsVisible_ = true;
        }
        if (needsVisible_) {
            RebuildVisible();
            needsVisible_ = false;
        }

        DrawToolbar();
        ImGui::Spacing();
        const bool showDistribution = hasSelection_ && selectionGpu_ == showGpu_;
        const float panelHeight = std::max(ImGui::GetTextLineHeight() * 11.0f, infoHeight_ + ImGui::GetStyle().WindowPadding.y * 2.0f + 2.0f);
        DrawTable(showDistribution ? panelHeight : 0.0f);
        if (showDistribution) {
            DrawDistribution(frames, view);
        }
    }

    void ZoneStatsView::ShowName(const std::string& name, bool gpu)
    {
        std::snprintf(nameFilter_, sizeof(nameFilter_), "%s", name.c_str());
        kindFilter_ = -1;
        showGpu_ = gpu;
        needsVisible_ = true;
    }

    void ZoneStatsView::DrawToolbar()
    {
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.35f);
        ImGui::TextUnformatted("区間の統計");
        ImGui::PopFont();

        const std::vector<ZoneStatRow>& rows = showGpu_ ? gpuRows_ : cpuRows_;
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
        if (waitingForStatistics_) {
            ImGui::TextUnformatted("区間を集計しています…");
        } else {
            ImGui::Text("記録の全体 %s フレーム・区間は %s 種類。平均は 1 回あたり、その右は 1 フレームあたりの時間です。",
                Text::FormatCount(frameCount_).c_str(), Text::FormatCount(rows.size()).c_str());
        }
        ImGui::PopStyleColor();
        ImGui::Spacing();

        if (Draw::ToggleButton("CPU", !showGpu_)) {
            showGpu_ = false;
            needsVisible_ = true;
        }
        ImGui::SameLine(0.0f, 2.0f);
        if (Draw::ToggleButton("GPU", showGpu_)) {
            showGpu_ = true;
            needsVisible_ = true;
        }

        if (!showGpu_) {
            ImGui::SameLine(0.0f, 18.0f);
            if (Draw::ToggleButton("すべて", kindFilter_ < 0)) {
                kindFilter_ = -1;
                needsVisible_ = true;
            }
            for (int i = 0; i < static_cast<int>(ZoneKind::Count); ++i) {
                const ZoneKind kind = static_cast<ZoneKind>(i);
                const ImVec4 color = ZoneKindColor(kind);
                ImGui::SameLine(0.0f, 4.0f);
                if (Draw::ToggleButton(ZoneKindLabel(kind), kindFilter_ == i, &color)) {
                    kindFilter_ = (kindFilter_ == i) ? -1 : i;
                    needsVisible_ = true;
                }
            }
        }

        ImGui::SameLine(0.0f, 18.0f);
        ImGui::SetNextItemWidth(std::max(160.0f, ImGui::GetContentRegionAvail().x));
        if (ImGui::InputTextWithHint("##nameFilter", "名前か場所で絞り込む", nameFilter_, sizeof(nameFilter_))) {
            needsVisible_ = true;
        }
    }

    void ZoneStatsView::DrawTable(float panelHeight)
    {
        const std::vector<ZoneStatRow>& rows = showGpu_ ? gpuRows_ : cpuRows_;
        if (rows.empty()) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
            ImGui::TextUnformatted(showGpu_ ? "GPU の区間はまだありません。" : "区間はまだありません。");
            ImGui::PopStyleColor();
            return;
        }

        const ImGuiTableFlags flags = ImGuiTableFlags_Sortable | ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable
            | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterH
            | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
        const float footerHeight = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
        if (!ImGui::BeginTable("##zoneStats", kColumnNum, flags, ImVec2(0.0f, -(footerHeight + panelHeight)))) {
            return;
        }

        const float numberWidth = ImGui::CalcTextSize("000,000.0").x;
        const float sortArrowWidth = ImGui::GetFontSize() + ImGui::GetStyle().ItemInnerSpacing.x;
        const auto numberColumnWidth = [&](const char* label) {
            return std::max(numberWidth, ImGui::CalcTextSize(label).x + sortArrowWidth);
        };
        constexpr ImGuiTableColumnFlags kNumberFlags = ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending;
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableSetupColumn("区間", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoHide, 0.0f, kColumnName);
        ImGui::TableSetupColumn("種類", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("■ フレームの枠").x, kColumnKind);
        ImGui::TableSetupColumn("回数", kNumberFlags, numberColumnWidth("回数"), kColumnCount);
        ImGui::TableSetupColumn("合計 ms", kNumberFlags | ImGuiTableColumnFlags_DefaultSort, numberColumnWidth("合計 ms"), kColumnTotal);
        ImGui::TableSetupColumn("平均 ms", kNumberFlags, numberColumnWidth("平均 ms"), kColumnMean);
        ImGui::TableSetupColumn("1 フレーム ms", kNumberFlags, numberColumnWidth("1 フレーム ms"), kColumnPerFrame);
        ImGui::TableSetupColumn("最大 ms", kNumberFlags, numberColumnWidth("最大 ms"), kColumnMax);
        ImGui::TableSetupColumn("ばらつき ms", kNumberFlags, numberColumnWidth("ばらつき ms"), kColumnStddev);
        ImGui::TableSetupColumn("場所", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("GameObjectManager.cpp:182").x, kColumnLocation);
        ImGui::TableHeadersRow();

        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsDirty) {
            if (specs->SpecsCount > 0) {
                sortColumn_ = static_cast<int>(specs->Specs[0].ColumnUserID);
                sortAscending_ = specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
            }
            specs->SpecsDirty = false;
            SortRows(cpuRows_);
            SortRows(gpuRows_);
            RebuildVisible();
        }

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(visible_.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const ZoneStatRow& row = *visible_[static_cast<size_t>(i)];
                ImGui::TableNextRow();
                ImGui::PushID(i);

                ImGui::TableSetColumnIndex(kColumnName);
                const bool selected = hasSelection_ && selectionGpu_ == showGpu_ && selectionSrcloc_ == row.srcloc;
                if (ImGui::Selectable("##row", selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
                    hasSelection_ = true;
                    selectionGpu_ = showGpu_;
                    selectionSrcloc_ = row.srcloc;
                }
                if (ImGui::BeginItemTooltip()) {
                    ImGui::PushTextWrapPos(ImGui::GetTextLineHeight() * 32.0f);
                    ImGui::TextUnformatted(row.name.c_str());
                    const std::string description = ZoneDescriptions::Instance().Describe(row.name, row.file);
                    if (!description.empty()) {
                        ImGui::TextUnformatted(description.c_str());
                    }
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
                    ImGui::Text("%s・%s:%u", ZoneKindLabel(row.kind), row.file.c_str(), row.line);
                    ImGui::PopStyleColor();
                    ImGui::Text("1 回あたり %s ms・1 フレームあたり %s ms・最大 %s ms",
                        Text::FormatMs(row.mean).c_str(), Text::FormatMs(row.perFrame).c_str(),
                        Text::FormatMs(static_cast<double>(row.max)).c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
                if (ImGui::BeginPopupContextItem("##rowMenu")) {
                    if (ImGui::MenuItem("名前をコピー")) {
                        ImGui::SetClipboardText(row.name.c_str());
                    }
                    if (ImGui::MenuItem("場所をコピー")) {
                        ImGui::SetClipboardText(std::format("{}:{}", row.file, row.line).c_str());
                    }
                    ImGui::EndPopup();
                }
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::TextUnformatted(row.name.c_str());

                ImGui::TableSetColumnIndex(kColumnKind);
                KindSwatch(row.kind);
                ImGui::TextUnformatted(ZoneKindLabel(row.kind));

                ImGui::TableSetColumnIndex(kColumnCount);
                TextRight(Text::FormatCount(row.count));
                ImGui::TableSetColumnIndex(kColumnTotal);
                TextRight(Text::FormatMs(static_cast<double>(row.total)));
                ImGui::TableSetColumnIndex(kColumnMean);
                TextRight(Text::FormatMs(row.mean));
                ImGui::TableSetColumnIndex(kColumnPerFrame);
                TextRight(Text::FormatMs(row.perFrame));
                ImGui::TableSetColumnIndex(kColumnMax);
                TextRight(Text::FormatMs(static_cast<double>(row.max)));
                ImGui::TableSetColumnIndex(kColumnStddev);
                TextRight(Text::FormatMs(row.stddev));

                ImGui::TableSetColumnIndex(kColumnLocation);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
                ImGui::TextUnformatted(row.location.c_str());
                ImGui::PopStyleColor();

                ImGui::PopID();
            }
        }
        ImGui::EndTable();

        ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextMute);
        if (visible_.empty()) {
            ImGui::TextUnformatted("当てはまる区間がありません。");
        } else {
            ImGui::Text("%s / %s 種類を表示。行をクリックすると 1 回ごとの時間の分布が出ます。右クリックで名前と場所をコピーできます。",
                Text::FormatCount(visible_.size()).c_str(), Text::FormatCount(rows.size()).c_str());
        }
        ImGui::PopStyleColor();
    }

    void ZoneStatsView::DrawDistribution(const FrameSnapshot& frames, ViewState& view)
    {
        const float lineHeight = ImGui::GetTextLineHeight();
        const Distribution& distribution = distribution_;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::kWindow);
        ImGui::BeginChild("##statsSelection", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
        const float available = ImGui::GetContentRegionAvail().x;
        const float infoWidth = std::clamp(available * 0.25f, lineHeight * 18.0f, lineHeight * 34.0f);
        ImGui::BeginChild("##statsInfo", ImVec2(infoWidth, 0.0f));

        ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
        ImGui::TextUnformatted("選んだ行");
        ImGui::PopStyleColor();
        if (!distribution.valid) {
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextMute);
            ImGui::TextUnformatted("集計しています…");
            ImGui::PopStyleColor();
        } else {
            ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.35f);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(distribution.name.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopFont();
            ImGui::PushTextWrapPos(0.0f);
            if (!distribution.description.empty()) {
                ImGui::TextUnformatted(distribution.description.c_str());
            }
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
            ImGui::Text("%s%s", distribution.gpu ? "GPU・" : "", distribution.location.c_str());
            ImGui::PopStyleColor();
            ImGui::Spacing();

            std::string sentence;
            if (distribution.count == 0) {
                sentence = "終わった区間がまだありません。";
            } else {
                sentence = std::format("{} 回のうち半分は {} 以下で、いちばん長い 1 回は {}（フレーム {}）です。",
                    Text::FormatCount(distribution.count), Ms(static_cast<double>(distribution.median)),
                    Ms(static_cast<double>(distribution.max)), Text::FormatCount(frames.Number(distribution.maxFrame)));
                if (distribution.overTarget > 0) {
                    sentence += std::format("目標の {:.1f} ms を超えた回が {} 回あります。",
                        static_cast<double>(distribution.target) / 1'000'000.0, Text::FormatCount(distribution.overTarget));
                }
            }
            ImGui::TextUnformatted(sentence.c_str());
            ImGui::PopTextWrapPos();
        }

        ImGui::Spacing();
        ImGui::BeginDisabled(!distribution.valid || !distribution.maxRef.IsValid());
        if (Draw::AccentButton("最大のフレームを開く")) {
            SelectFrame(view, frames, distribution.maxFrame);
            view.followLatest = false;
            view.selectedZone = distribution.maxRef;
            view.tab = ViewTab::Timeline;
            view.tabRequested = true;
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("いちばん長かった 1 回のフレームを選び、タイムラインでその区間を開きます。");
        ImGui::SameLine();
        if (ImGui::Button("選ぶのをやめる")) {
            hasSelection_ = false;
        }
        infoHeight_ = ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y;
        ImGui::EndChild();

        ImGui::SameLine(0.0f, lineHeight * 1.5f);
        ImGui::BeginChild("##statsHistogram", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        if (distribution.valid && distribution.count > 0) {
            DrawHistogram();
        }
        ImGui::EndChild();

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    void ZoneStatsView::DrawHistogram()
    {
        const float lineHeight = ImGui::GetTextLineHeight();
        const Distribution& distribution = distribution_;

        ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
        const char* caption = "1 回ごとの時間の分布（横は対数）";
        const char* scale = "縦は回数（多い所が飛び出さないよう、平方根で縮めている）";
        const float lineStart = ImGui::GetCursorPosX();
        const float lineWidth = ImGui::GetContentRegionAvail().x;
        const float scaleWidth = ImGui::CalcTextSize(scale).x;
        ImGui::TextUnformatted(caption);
        if (ImGui::CalcTextSize(caption).x + scaleWidth + lineHeight * 2.0f <= lineWidth) {
            ImGui::SameLine(lineStart + lineWidth - scaleWidth);
        }
        ImGui::TextUnformatted(scale);
        ImGui::PopStyleColor();

        const float tickHeight = lineHeight + 6.0f;
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 size(std::max(1.0f, ImGui::GetContentRegionAvail().x),
            std::max(lineHeight * 4.0f, ImGui::GetContentRegionAvail().y - tickHeight));
        ImGui::InvisibleButton("##histogramArea", size);
        const bool hovered = ImGui::IsItemHovered();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImGui::GetColorU32(Theme::kField));
        drawList->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImGui::GetColorU32(Theme::kOutline));

        const size_t binCount = distribution.bins.size();
        const float slot = size.x / static_cast<float>(std::max<size_t>(1, binCount));
        const float bottom = pos.y + size.y - 1.0f;
        const float height = size.y - lineHeight * 1.8f;
        uint32_t most = 1;
        for (const uint32_t count : distribution.bins) {
            most = std::max(most, count);
        }
        const auto edge = [&](size_t bin) {
            return std::pow(10.0, distribution.firstDecade + static_cast<double>(bin) / kBinsPerDecade);
        };
        const auto xOf = [&](double nanoseconds) {
            const double position = (std::log10(std::max(nanoseconds, 1.0)) - distribution.firstDecade) / distribution.decades;
            return pos.x + static_cast<float>(position) * size.x;
        };
        for (size_t bin = 0; bin < binCount; ++bin) {
            const uint32_t count = distribution.bins[bin];
            if (count == 0) {
                continue;
            }
            const float barHeight = std::max(2.0f, std::sqrt(static_cast<float>(count) / static_cast<float>(most)) * height);
            const float x0 = pos.x + static_cast<float>(bin) * slot;
            const float x1 = x0 + std::max(1.0f, slot >= 4.0f ? slot - 2.0f : slot);
            const ImVec4 color = LevelColor(static_cast<int64_t>(edge(bin)), distribution.target);
            drawList->AddRectFilled(ImVec2(x0, bottom - barHeight), ImVec2(x1, bottom), ImGui::GetColorU32(color));
        }

        // 目標の線
        const double target = static_cast<double>(distribution.target);
        if (target > edge(0) && target < edge(binCount)) {
            const float x = xOf(target);
            for (float y = pos.y + 2.0f; y < bottom; y += 9.0f) {
                drawList->AddLine(ImVec2(x, y), ImVec2(x, std::min(y + 5.0f, bottom)), ImGui::GetColorU32(Theme::kWarm));
            }
            const std::string label = std::format("目標 {:.1f} ms", target / 1'000'000.0);
            drawList->AddText(ImVec2(x + 4.0f, pos.y + 3.0f), ImGui::GetColorU32(Theme::kWarm), label.c_str());
        }

        // いちばん長い 1 回
        {
            const std::string label = "最大 " + Ms(static_cast<double>(distribution.max));
            const ImVec2 labelSize = ImGui::CalcTextSize(label.c_str());
            const float padding = lineHeight * 0.3f;
            const ImVec4 background = FrameLevelOf(distribution.max, distribution.target) == FrameLevel::Normal ? Theme::kControl : Theme::kError;
            const float left = pos.x + size.x - labelSize.x - padding * 2.0f - 4.0f;
            const float top = pos.y + lineHeight * 1.3f;
            drawList->AddRectFilled(ImVec2(left, top), ImVec2(left + labelSize.x + padding * 2.0f, top + labelSize.y + 2.0f),
                ImGui::GetColorU32(background));
            drawList->AddText(ImVec2(left + padding, top + 1.0f), Draw::TextOn(background), label.c_str());
        }

        // 10 の累乗ごとの目盛り
        for (int k = 0; k <= distribution.decades; ++k) {
            const float x = pos.x + static_cast<float>(k) / static_cast<float>(distribution.decades) * size.x;
            drawList->AddLine(ImVec2(x, bottom), ImVec2(x, bottom + 4.0f), ImGui::GetColorU32(Theme::kTextMute));
            const std::string label = DecadeLabel(distribution.firstDecade + k);
            const float labelWidth = ImGui::CalcTextSize(label.c_str()).x;
            const float left = std::clamp(x - labelWidth * 0.5f, pos.x, pos.x + size.x - labelWidth);
            drawList->AddText(ImVec2(left, bottom + 5.0f), ImGui::GetColorU32(Theme::kTextDim), label.c_str());
        }

        if (hovered && binCount > 0) {
            const float mouseX = ImGui::GetIO().MousePos.x;
            const size_t bin = std::min(binCount - 1, static_cast<size_t>(std::max(0.0f, (mouseX - pos.x) / slot)));
            const float x0 = pos.x + static_cast<float>(bin) * slot;
            drawList->AddRectFilled(ImVec2(x0, pos.y), ImVec2(x0 + slot, bottom), ImGui::GetColorU32(Theme::WithAlpha(Theme::kText, 0.10f)));
            ImGui::SetTooltip("%s 〜 %s ms：%s 回", Text::FormatMs(edge(bin)).c_str(), Text::FormatMs(edge(bin + 1)).c_str(),
                Text::FormatCount(distribution.bins[bin]).c_str());
        }
        ImGui::Dummy(ImVec2(size.x, tickHeight));
    }
}
