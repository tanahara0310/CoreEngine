#include "View/ZoneStatsView.h"

#include "Editor/ImGui/EditorTheme.h"
#include "Text/TextFormat.h"
#include "TracyWorker.hpp"

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

        /// @brief 押した状態を下地の色で見せるボタン（CPU と GPU・種類の切り替え）
        bool ToggleButton(const char* label, bool selected, const ZoneKind* swatch = nullptr)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, selected ? Theme::kAccentMuted : Theme::kControl);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, selected ? Theme::kAccentMuted : Theme::kHover);
            ImGui::PushStyleColor(ImGuiCol_Text, selected ? Theme::kText : Theme::kTextDim);

            std::string text = label;
            float swatchSize = 0.0f;
            if (swatch) {
                swatchSize = ImGui::GetTextLineHeight() * 0.55f;
                const float spaceWidth = ImGui::CalcTextSize(" ").x;
                const int spaces = static_cast<int>(std::ceil((swatchSize + ImGui::GetStyle().ItemInnerSpacing.x) / spaceWidth));
                text.insert(0, static_cast<size_t>(spaces), ' ');
            }
            const bool pressed = ImGui::Button(text.c_str());
            if (swatch) {
                const ImVec2 min = ImGui::GetItemRectMin();
                const ImVec2 max = ImGui::GetItemRectMax();
                const float left = min.x + ImGui::GetStyle().FramePadding.x;
                const float top = (min.y + max.y - swatchSize) * 0.5f;
                ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(left, top), ImVec2(left + swatchSize, top + swatchSize),
                    ImGui::GetColorU32(ZoneKindColor(*swatch)), 1.5f);
            }
            ImGui::PopStyleColor(3);
            return pressed;
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

    void ZoneStatsView::Draw()
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
        DrawTable();
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

        if (ToggleButton("CPU", !showGpu_)) {
            showGpu_ = false;
            needsVisible_ = true;
        }
        ImGui::SameLine(0.0f, 2.0f);
        if (ToggleButton("GPU", showGpu_)) {
            showGpu_ = true;
            needsVisible_ = true;
        }

        if (!showGpu_) {
            ImGui::SameLine(0.0f, 18.0f);
            if (ToggleButton("すべて", kindFilter_ < 0)) {
                kindFilter_ = -1;
                needsVisible_ = true;
            }
            for (int i = 0; i < static_cast<int>(ZoneKind::Count); ++i) {
                const ZoneKind kind = static_cast<ZoneKind>(i);
                ImGui::SameLine(0.0f, 4.0f);
                if (ToggleButton(ZoneKindLabel(kind), kindFilter_ == i, &kind)) {
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

    void ZoneStatsView::DrawTable()
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
        if (!ImGui::BeginTable("##zoneStats", kColumnNum, flags, ImVec2(0.0f, -footerHeight))) {
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
                ImGui::Selectable("##row", false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap);
                if (ImGui::BeginItemTooltip()) {
                    ImGui::TextUnformatted(row.name.c_str());
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
                    ImGui::Text("%s・%s:%u", ZoneKindLabel(row.kind), row.file.c_str(), row.line);
                    ImGui::PopStyleColor();
                    ImGui::Text("1 回あたり %s ms・1 フレームあたり %s ms・最大 %s ms",
                        Text::FormatMs(row.mean).c_str(), Text::FormatMs(row.perFrame).c_str(),
                        Text::FormatMs(static_cast<double>(row.max)).c_str());
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
            ImGui::Text("%s / %s 種類を表示。行の上で右クリックすると名前と場所をコピーできます。",
                Text::FormatCount(visible_.size()).c_str(), Text::FormatCount(rows.size()).c_str());
        }
        ImGui::PopStyleColor();
    }
}
