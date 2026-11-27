#include "View/UpdateTreeView.h"

#include "App/ViewState.h"
#include "Capture/FrameSnapshot.h"
#include "Text/TextFormat.h"
#include "View/DrawHelpers.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

namespace CoreEngine::Profiler
{
    namespace
    {
        namespace Theme = CoreEngine::Editor::Theme;

        /// @brief まとめる小さな区間（1 フレームあたりがこれ未満で、最大が kSmallMax 未満）
        constexpr double kSmallMean = 10'000.0;
        constexpr int64_t kSmallMax = 1'000'000;

        /// @brief 既定で開く区間（1 フレームあたりが Update のこの割合以上）
        constexpr double kOpenShare = 0.05;

        /// @brief 「重い 1 回」とみなす時間（ナノ秒）
        constexpr int64_t kSpikeNs = 1'000'000;

        ImVec4 GroupColor(UpdateNodeGroup group)
        {
            switch (group) {
            case UpdateNodeGroup::Root: return Theme::kOk;
            case UpdateNodeGroup::Phase: return Theme::FromSrgb(63, 158, 101);
            case UpdateNodeGroup::Feature: return Theme::kAccentHover;
            case UpdateNodeGroup::Object: return Theme::FromSrgb(201, 133, 0);
            case UpdateNodeGroup::Script: return Theme::kScript;
            default: return Theme::FromSrgb(110, 110, 122);
            }
        }

        /// @brief 「自分だけ」と「ほか n 個」の色
        ImVec4 RestColor()
        {
            return Theme::FromSrgb(90, 90, 100);
        }

        std::string Ms(double nanoseconds)
        {
            return Text::FormatMs(nanoseconds) + " ms";
        }

        /// @brief グラフの縦の上限の数字（1・2・5 の値なので短く出す）
        std::string ScaleMs(double nanoseconds)
        {
            return std::format("{:g} ms", nanoseconds / 1'000'000.0);
        }

        std::string CallsPerFrame(double calls)
        {
            if (calls >= 0.95) {
                const double rounded = std::round(calls);
                return std::abs(calls - rounded) < 0.05 ? std::format("{:.0f}", rounded) : std::format("{:.1f}", calls);
            }
            return calls >= 0.01 ? std::format("{:.2f}", calls) : std::string("0.01 未満");
        }

        ImVec4 MaxColor(int64_t max, int64_t target)
        {
            if (FrameLevelOf(max, target) != FrameLevel::Normal) {
                return Theme::kError;
            }
            return max >= kSpikeNs ? Theme::kWarm : Theme::kText;
        }

        const char* FrameClassLabel(FrameClass frameClass)
        {
            switch (frameClass) {
            case FrameClass::Stopped: return "停止中";
            case FrameClass::Play: return "再生中";
            case FrameClass::FirstPlay: return "再生の最初のフレーム";
            default: return "Update の区間なし";
            }
        }

        /// @brief 根からその節までの名前（Update/PostLogic/EnvironmentFeature）
        std::string PathOf(const UpdateTree& tree, uint32_t node)
        {
            std::string path = tree.Node(node).name;
            while (node != UpdateTree::kRoot) {
                node = tree.Node(node).parent;
                path = tree.Node(node).name + "/" + path;
            }
            return path;
        }

        /// @brief 選んだ区間をタイムラインで開く
        void OpenInTimeline(ViewState& view, const FrameSnapshot& frames, size_t frame, const tracy::ZoneEvent* zone, uint64_t thread)
        {
            SelectFrame(view, frames, frame);
            view.followLatest = false;
            if (zone) {
                view.selectedZone = ZoneRef{ zone, nullptr, thread };
            }
            view.tab = ViewTab::Timeline;
            view.tabRequested = true;
        }
    }

    void UpdateTreeView::Reset()
    {
        expanded_.clear();
        groupOpen_.clear();
        flatGroupOpen_ = false;
        rows_.clear();
        selectedNode_ = UpdateTree::kRoot;
        selectedSelf_ = false;
    }

    void UpdateTreeView::Draw(UpdateTree& tree, const FrameSnapshot& frames, ViewState& view)
    {
        if (!tree.HasRoot()) {
            ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.35f);
            ImGui::TextUnformatted("Update の内訳");
            ImGui::PopFont();
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
            if (tree.ProcessedFrames() < tree.AvailableFrames()) {
                ImGui::TextUnformatted("集計しています…");
            } else {
                ImGui::TextUnformatted("この記録には、メインスレッドの Update の区間（Framework.cpp）がありません。");
            }
            ImGui::PopStyleColor();
            return;
        }
        if (selectedNode_ >= tree.Nodes().size()) {
            selectedNode_ = UpdateTree::kRoot;
            selectedSelf_ = false;
        }
        tree.RequestTrend(selectedNode_, selectedSelf_);

        DrawToolbar(tree, frames, view);
        ImGui::Spacing();
        BuildRows(tree);

        const float lineHeight = ImGui::GetTextLineHeight();
        const float panelHeight = std::max(lineHeight * 12.0f, infoHeight_ + ImGui::GetStyle().WindowPadding.y * 2.0f + 2.0f);
        const float tableHeight = std::max(lineHeight * 6.0f, ImGui::GetContentRegionAvail().y - panelHeight - ImGui::GetStyle().ItemSpacing.y);
        DrawTable(tree, frames, view, tableHeight);
        DrawSelection(tree, frames, view);
    }

    UpdateTreeView::Row UpdateTreeView::MakeRow(const UpdateTree& tree, RowKind kind, uint32_t node, uint32_t depth) const
    {
        Row row;
        row.kind = kind;
        row.node = node;
        row.depth = depth;
        row.summary = tree.Summarize(node, includeStopped_);
        const UpdateNodeSummary& summary = row.summary;
        if (kind == RowKind::Self) {
            row.values.mean = summary.selfMean;
            row.values.max = summary.maxSelf;
            row.values.maxZone = summary.maxSelfZone;
            row.values.maxFrame = summary.maxSelfFrame;
            row.values.spikes = summary.selfSpikes;
            row.values.selected = tree.SelectedSelf(node);
        } else {
            row.values.mean = summary.mean;
            row.values.max = summary.max;
            row.values.maxZone = summary.maxZone;
            row.values.maxFrame = summary.maxFrame;
            row.values.spikes = summary.spikes;
            row.values.selected = tree.SelectedTime(node);
        }
        return row;
    }

    UpdateTreeView::Row UpdateTreeView::MakeGroup(const UpdateTree& tree, uint32_t parent, uint32_t depth,
        const std::vector<std::pair<uint32_t, bool>>& members) const
    {
        Row group;
        group.kind = RowKind::Group;
        group.node = parent;
        group.depth = depth;
        group.members = members;
        group.hasToggle = true;
        group.summary.present = true;
        for (const auto& [node, self] : members) {
            const Row member = MakeRow(tree, self ? RowKind::Self : RowKind::Node, node, depth);
            group.values.mean += member.values.mean;
            group.values.spikes += member.values.spikes;
            if (member.values.max > group.values.max) {
                group.values.max = member.values.max;
                group.values.maxZone = member.values.maxZone;
                group.values.maxFrame = member.values.maxFrame;
            }
            if (member.values.selected >= 0) {
                group.values.selected = std::max<int64_t>(group.values.selected, 0) + member.values.selected;
            }
        }
        return group;
    }

    bool UpdateTreeView::IsSmall(const Row& row) const
    {
        return row.values.mean < kSmallMean && row.values.max < kSmallMax;
    }

    bool UpdateTreeView::Matches(const UpdateTree& tree, uint32_t node) const
    {
        const UpdateNode& target = tree.Node(node);
        return Text::ContainsIgnoreCase(target.name, filter_) || Text::ContainsIgnoreCase(target.description, filter_);
    }

    bool UpdateTreeView::SubtreeMatches(const UpdateTree& tree, uint32_t node) const
    {
        if (Matches(tree, node)) {
            return true;
        }
        for (const uint32_t child : tree.Node(node).children) {
            if (SubtreeMatches(tree, child)) {
                return true;
            }
        }
        return false;
    }

    bool UpdateTreeView::IsOpen(uint32_t node, double mean, double rootMean) const
    {
        if (node < expanded_.size() && expanded_[node] >= 0) {
            return expanded_[node] == 1;
        }
        return node == UpdateTree::kRoot || (rootMean > 0.0 && mean >= rootMean * kOpenShare);
    }

    void UpdateTreeView::Toggle(const Row& row)
    {
        if (row.kind == RowKind::Group) {
            if (row.node == UpdateTree::kNoNode) {
                flatGroupOpen_ = !flatGroupOpen_;
            } else if (row.node < groupOpen_.size()) {
                groupOpen_[row.node] = row.open ? 0 : 1;
            }
            return;
        }
        if (row.kind == RowKind::Node && row.node < expanded_.size()) {
            expanded_[row.node] = row.open ? 0 : 1;
        }
    }

    void UpdateTreeView::BuildRows(const UpdateTree& tree)
    {
        const size_t nodeCount = tree.Nodes().size();
        if (expanded_.size() < nodeCount) {
            expanded_.resize(nodeCount, -1);
            groupOpen_.resize(nodeCount, 0);
        }
        rows_.clear();
        if (sortMode_ == SortMode::Tree) {
            const double rootMean = tree.Summarize(UpdateTree::kRoot, includeStopped_).mean;
            AddTreeRows(tree, UpdateTree::kRoot, 0, rootMean, FilterActive());
        } else {
            AddFlatRows(tree);
        }
    }

    void UpdateTreeView::AddTreeRows(const UpdateTree& tree, uint32_t node, uint32_t depth, double rootMean, bool filtering)
    {
        Row row = MakeRow(tree, RowKind::Node, node, depth);
        if (!row.summary.present || (filtering && !SubtreeMatches(tree, node))) {
            return;
        }
        // 名前が当たった区間の下は、絞り込まずに出す
        const bool childFiltering = filtering && !Matches(tree, node);
        std::vector<uint32_t> children;
        for (const uint32_t child : tree.Node(node).children) {
            if (!tree.Summarize(child, includeStopped_).present || (childFiltering && !SubtreeMatches(tree, child))) {
                continue;
            }
            children.push_back(child);
        }
        row.hasToggle = !children.empty();
        row.open = row.hasToggle && (childFiltering || IsOpen(node, row.values.mean, rootMean));
        rows_.push_back(row);
        if (!row.open) {
            return;
        }

        std::vector<uint32_t> small;
        if (mergeSmall_ && !childFiltering && depth >= 1) {
            for (const uint32_t child : children) {
                if (IsSmall(MakeRow(tree, RowKind::Node, child, depth + 1))) {
                    small.push_back(child);
                }
            }
            if (small.size() < 2) {
                small.clear();
            }
        }
        for (const uint32_t child : children) {
            if (std::find(small.begin(), small.end(), child) == small.end()) {
                AddTreeRows(tree, child, depth + 1, rootMean, childFiltering);
            }
        }
        if (!small.empty()) {
            std::vector<std::pair<uint32_t, bool>> members;
            for (const uint32_t child : small) {
                members.emplace_back(child, false);
            }
            Row group = MakeGroup(tree, node, depth + 1, members);
            group.open = node < groupOpen_.size() && groupOpen_[node] != 0;
            rows_.push_back(group);
            if (group.open) {
                for (const uint32_t child : small) {
                    AddTreeRows(tree, child, depth + 2, rootMean, false);
                }
            }
        }

        const Row self = MakeRow(tree, RowKind::Self, node, depth + 1);
        const bool showSelf = !childFiltering && (!mergeSmall_ || !IsSmall(self));
        if (showSelf && (self.values.mean > 0.0 || self.values.max > 0)) {
            rows_.push_back(self);
        }
    }

    void UpdateTreeView::AddFlatRows(const UpdateTree& tree)
    {
        const bool filtering = FilterActive();
        std::vector<Row> items;
        for (uint32_t node = 0; node < tree.Nodes().size(); ++node) {
            if (!tree.Summarize(node, includeStopped_).present || (filtering && !Matches(tree, node))) {
                continue;
            }
            bool hasChildren = false;
            for (const uint32_t child : tree.Node(node).children) {
                hasChildren = hasChildren || tree.Summarize(child, includeStopped_).present;
            }
            Row row = MakeRow(tree, hasChildren ? RowKind::Self : RowKind::Node, node, 0);
            if (row.values.mean > 0.0 || row.values.max > 0) {
                items.push_back(std::move(row));
            }
        }
        const bool byMean = sortMode_ == SortMode::Mean;
        std::stable_sort(items.begin(), items.end(), [byMean](const Row& a, const Row& b) {
            return byMean ? a.values.mean > b.values.mean : a.values.max > b.values.max;
        });

        std::vector<std::pair<uint32_t, bool>> small;
        for (Row& item : items) {
            if (mergeSmall_ && !filtering && IsSmall(item)) {
                small.emplace_back(item.node, item.kind == RowKind::Self);
            } else {
                rows_.push_back(std::move(item));
            }
        }
        if (small.size() == 1) {
            rows_.push_back(MakeRow(tree, small[0].second ? RowKind::Self : RowKind::Node, small[0].first, 0));
        } else if (!small.empty()) {
            Row group = MakeGroup(tree, UpdateTree::kNoNode, 0, small);
            group.open = flatGroupOpen_;
            rows_.push_back(group);
            if (group.open) {
                for (const auto& [node, self] : small) {
                    rows_.push_back(MakeRow(tree, self ? RowKind::Self : RowKind::Node, node, 1));
                }
            }
        }
    }

    void UpdateTreeView::DrawToolbar(const UpdateTree& tree, const FrameSnapshot& frames, ViewState& view)
    {
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.35f);
        ImGui::TextUnformatted("Update の内訳");
        ImGui::PopFont();

        std::string subtitle;
        const std::string counted = Text::FormatCount(tree.CountedFrames(includeStopped_));
        if (!tree.HasPlayFrames()) {
            subtitle = std::format("この記録の {} フレームの平均。再生中のフレーム（GameObject Update の区間があるフレーム）が無いので、全部のフレームを数えています。", counted);
        } else {
            subtitle = std::format("{}の {} フレームの平均（再生の最初の {} フレームは平均に入れず、最大にだけ入れる）",
                includeStopped_ ? "再生中と停止中" : "再生中", counted, Text::FormatCount(tree.FrameCount(FrameClass::FirstPlay)));
        }
        if (tree.ProcessedFrames() < tree.AvailableFrames()) {
            subtitle += std::format("　集計しています… {} / {} フレーム", Text::FormatCount(tree.ProcessedFrames()),
                Text::FormatCount(tree.AvailableFrames()));
        }
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(subtitle.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();

        const std::vector<size_t>& firsts = tree.FirstPlayFrames();
        if (!firsts.empty()) {
            size_t heaviest = firsts.front();
            for (const size_t frame : firsts) {
                if (tree.RootTime(frame) > tree.RootTime(heaviest)) {
                    heaviest = frame;
                }
            }
            const std::string number = Text::FormatCount(frames.Number(heaviest));
            const std::string time = Ms(static_cast<double>(tree.RootTime(heaviest)));
            const std::string text = firsts.size() == 1
                ? std::format("再生の最初のフレーム：フレーム {}（Update {}）", number, time)
                : std::format("再生の最初のフレームは {} 回。いちばん重いのはフレーム {}（Update {}）", firsts.size(), number, time);
            ImGui::AlignTextToFramePadding();
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
            ImGui::TextUnformatted(text.c_str());
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if (ImGui::SmallButton("そのフレームを開く")) {
                OpenInTimeline(view, frames, heaviest, tree.RootZone(heaviest), tree.MainThreadId());
            }
        }
        ImGui::Spacing();

        if (Draw::ToggleButton("段の順", sortMode_ == SortMode::Tree)) {
            sortMode_ = SortMode::Tree;
        }
        ImGui::SameLine(0.0f, 2.0f);
        if (Draw::ToggleButton("平均の重い順", sortMode_ == SortMode::Mean)) {
            sortMode_ = SortMode::Mean;
        }
        ImGui::SameLine(0.0f, 2.0f);
        if (Draw::ToggleButton("最大の重い順", sortMode_ == SortMode::Max)) {
            sortMode_ = SortMode::Max;
        }
        ImGui::SetItemTooltip("重い順のときは木をほどき、中に区間の無い区間と「自分だけ」を並べます。");

        ImGui::SameLine(0.0f, 18.0f);
        ImGui::Checkbox("0.01 ms 未満の区間はまとめる", &mergeSmall_);
        ImGui::SetItemTooltip("1 フレームあたりが 0.01 ms 未満で、最大も 1 ms 未満の区間を「ほか n 個」にまとめます。");
        ImGui::SameLine(0.0f, 18.0f);
        ImGui::BeginDisabled(!tree.HasPlayFrames());
        ImGui::Checkbox("停止中のフレームも含める", &includeStopped_);
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("停止中は動く Feature が違う（エディタの操作など）ので、既定は再生中のフレームだけを数えます。");

        ImGui::SameLine(0.0f, 18.0f);
        ImGui::SetNextItemWidth(std::max(160.0f, ImGui::GetContentRegionAvail().x));
        ImGui::InputTextWithHint("##updateFilter", "区間の名前か説明で絞り込む", filter_, sizeof(filter_));
    }

    void UpdateTreeView::DrawTable(const UpdateTree& tree, const FrameSnapshot& frames, const ViewState& view, float height)
    {
        const ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV
            | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
        if (!ImGui::BeginTable("##updateTree", 7, flags, ImVec2(0.0f, height))) {
            return;
        }
        const float lineHeight = ImGui::GetTextLineHeight();
        const float numberWidth = ImGui::CalcTextSize("000.000 ms").x;
        const auto fixedWidth = [&](const char* label) { return std::max(numberWidth, ImGui::CalcTextSize(label).x); };
        const std::string frameLabel = tree.HasSelectedFrame()
            ? std::format("フレーム {}###frame", Text::FormatCount(frames.Number(tree.SelectedFrame())))
            : std::string("選んだフレーム###frame");
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("区間", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoHide, 1.2f);
        ImGui::TableSetupColumn("何をしているか", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("平均", ImGuiTableColumnFlags_WidthFixed, fixedWidth("平均"));
        ImGui::TableSetupColumn(frameLabel.c_str(), ImGuiTableColumnFlags_WidthFixed, fixedWidth("フレーム 000,000"));
        ImGui::TableSetupColumn("最大（1 回）", ImGuiTableColumnFlags_WidthFixed, fixedWidth("最大（1 回）"));
        ImGui::TableSetupColumn("回／フレーム", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("回／フレーム").x);
        ImGui::TableSetupColumn("Update に占める割合", ImGuiTableColumnFlags_WidthFixed, lineHeight * 9.0f);
        ImGui::TableHeadersRow();

        const double rootMean = tree.Summarize(UpdateTree::kRoot, includeStopped_).mean;
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows_.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                ImGui::PushID(i);
                DrawRow(tree, rows_[static_cast<size_t>(i)], rootMean, view);
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
        if (rows_.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextMute);
            ImGui::TextUnformatted("当てはまる区間がありません。");
            ImGui::PopStyleColor();
        }
    }

    void UpdateTreeView::DrawRow(const UpdateTree& tree, const Row& row, double rootMean, const ViewState& view)
    {
        const float rowHeight = ImGui::GetFrameHeight();
        const float lineHeight = ImGui::GetTextLineHeight();
        ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
        if (row.kind == RowKind::Node && row.depth == 0 && sortMode_ == SortMode::Tree) {
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(Theme::kPanel));
        }

        // 区間（開閉・色・名前）
        ImGui::TableSetColumnIndex(0);
        const ImVec2 cell = ImGui::GetCursorScreenPos();
        const bool selected = row.kind != RowKind::Group && row.node == selectedNode_ && (row.kind == RowKind::Self) == selectedSelf_;
        if (ImGui::Selectable("##row", selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap
                | ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0.0f, rowHeight))) {
            if (row.kind == RowKind::Group) {
                Toggle(row);
            } else {
                selectedNode_ = row.node;
                selectedSelf_ = row.kind == RowKind::Self;
                if (row.hasToggle && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    Toggle(row);
                }
            }
        }

        const UpdateNode* node = row.node != UpdateTree::kNoNode ? &tree.Node(row.node) : nullptr;
        std::string name;
        std::string description;
        ImVec4 swatch = RestColor();
        switch (row.kind) {
        case RowKind::Node:
            name = node->name;
            description = node->description;
            swatch = GroupColor(node->group);
            break;
        case RowKind::Self:
            name = sortMode_ == SortMode::Tree ? std::string("自分だけ") : node->name + " の自分だけ";
            description = node->selfDescription;
            break;
        case RowKind::Group:
            name = std::format("ほか {} 個", row.members.size());
            for (const auto& [member, self] : row.members) {
                if (!description.empty()) {
                    description += "・";
                }
                description += tree.Node(member).name;
                if (self) {
                    description += " の自分だけ";
                }
            }
            break;
        }

        if (ImGui::BeginItemTooltip()) {
            ImGui::PushTextWrapPos(lineHeight * 32.0f);
            if (row.kind == RowKind::Group) {
                ImGui::TextUnformatted(name.c_str());
            } else {
                ImGui::TextUnformatted(PathOf(tree, row.node).c_str());
                if (row.kind == RowKind::Self) {
                    ImGui::SameLine();
                    ImGui::TextUnformatted("の自分だけ");
                }
            }
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
            if (!description.empty()) {
                ImGui::TextUnformatted(description.c_str());
            }
            ImGui::Text("1 フレームあたり %s・最大 %s・1 ms 以上 %s %s", Ms(row.values.mean).c_str(),
                Ms(static_cast<double>(row.values.max)).c_str(), Text::FormatCount(row.values.spikes).c_str(),
                row.kind == RowKind::Self ? "フレーム" : "回");
            ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float centerY = cell.y + rowHeight * 0.5f;
        float x = cell.x + static_cast<float>(row.depth) * lineHeight * 1.1f;
        const float toggleWidth = lineHeight * 1.1f;
        if (row.hasToggle) {
            ImGui::SetCursorScreenPos(ImVec2(x, cell.y));
            if (ImGui::InvisibleButton("##toggle", ImVec2(toggleWidth, rowHeight))) {
                Toggle(row);
            }
            const ImU32 arrow = ImGui::GetColorU32(ImGui::IsItemHovered() ? Theme::kText : Theme::kTextDim);
            const float half = lineHeight * 0.22f;
            const float cx = x + toggleWidth * 0.5f;
            if (row.open) {
                drawList->AddTriangleFilled(ImVec2(cx - half, centerY - half * 0.6f), ImVec2(cx + half, centerY - half * 0.6f),
                    ImVec2(cx, centerY + half * 0.8f), arrow);
            } else {
                drawList->AddTriangleFilled(ImVec2(cx - half * 0.6f, centerY - half), ImVec2(cx - half * 0.6f, centerY + half),
                    ImVec2(cx + half * 0.8f, centerY), arrow);
            }
        }
        x += toggleWidth;
        const float swatchSize = lineHeight * 0.55f;
        drawList->AddRectFilled(ImVec2(x, centerY - swatchSize * 0.5f), ImVec2(x + swatchSize, centerY + swatchSize * 0.5f),
            ImGui::GetColorU32(swatch));
        x += swatchSize + ImGui::GetStyle().ItemInnerSpacing.x;
        const ImVec4 nameColor = row.kind == RowKind::Node ? Theme::kText : Theme::kTextDim;
        drawList->AddText(ImVec2(x, centerY - lineHeight * 0.5f), ImGui::GetColorU32(nameColor), name.c_str());

        // 何をしているか
        ImGui::TableSetColumnIndex(1);
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
        ImGui::TextUnformatted(description.c_str());
        ImGui::PopStyleColor();

        // 平均・選んだフレーム・最大・回数
        const bool firstOnly = row.kind == RowKind::Node && row.summary.firstOnly;
        ImGui::TableSetColumnIndex(2);
        ImGui::AlignTextToFramePadding();
        Draw::TextRight(firstOnly ? "—" : Ms(row.values.mean).c_str(), Theme::kText);
        ImGui::TableSetColumnIndex(3);
        ImGui::AlignTextToFramePadding();
        if (row.values.selected >= 0) {
            Draw::TextRight(row.values.selected > 0 ? Ms(static_cast<double>(row.values.selected)).c_str() : "—", Theme::kTextDim);
        }
        ImGui::TableSetColumnIndex(4);
        ImGui::AlignTextToFramePadding();
        Draw::TextRight(Ms(static_cast<double>(row.values.max)).c_str(), MaxColor(row.values.max, view.TargetNs()));
        ImGui::TableSetColumnIndex(5);
        ImGui::AlignTextToFramePadding();
        if (row.kind == RowKind::Node) {
            const std::string calls = firstOnly ? (row.summary.allCalls == 1 ? std::string("1 回だけ") : std::string("最初だけ"))
                                                : CallsPerFrame(row.summary.callsPerFrame);
            Draw::TextRight(calls.c_str(), Theme::kTextDim);
        }

        // Update に占める割合
        ImGui::TableSetColumnIndex(6);
        const ImVec2 barPos = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        const double share = rootMean > 0.0 ? std::clamp(row.values.mean / rootMean, 0.0, 1.0) : 0.0;
        const std::string percent = share > 0.0 && share < 0.01 ? std::string("< 1 %") : std::format("{:.0f} %", share * 100.0);
        const float textWidth = ImGui::CalcTextSize("< 1 %").x;
        const float barWidth = std::max(0.0f, width - textWidth - ImGui::GetStyle().ItemSpacing.x);
        const float barHeight = lineHeight * 0.5f;
        const float barTop = barPos.y + (rowHeight - barHeight) * 0.5f;
        drawList->AddRectFilled(ImVec2(barPos.x, barTop), ImVec2(barPos.x + barWidth, barTop + barHeight), ImGui::GetColorU32(Theme::kField));
        if (!firstOnly) {
            drawList->AddRectFilled(ImVec2(barPos.x, barTop), ImVec2(barPos.x + barWidth * static_cast<float>(share), barTop + barHeight),
                ImGui::GetColorU32(row.kind == RowKind::Node ? swatch : RestColor()));
            const float percentWidth = ImGui::CalcTextSize(percent.c_str()).x;
            drawList->AddText(ImVec2(barPos.x + width - percentWidth, barPos.y + (rowHeight - lineHeight) * 0.5f),
                ImGui::GetColorU32(Theme::kTextDim), percent.c_str());
        }
    }

    void UpdateTreeView::DrawSelection(UpdateTree& tree, const FrameSnapshot& frames, ViewState& view)
    {
        const float lineHeight = ImGui::GetTextLineHeight();
        Row row;
        bool found = false;
        for (const Row& candidate : rows_) {
            if (candidate.kind != RowKind::Group && candidate.node == selectedNode_ && (candidate.kind == RowKind::Self) == selectedSelf_) {
                row = candidate;
                found = true;
                break;
            }
        }
        if (!found) {
            row = MakeRow(tree, selectedSelf_ ? RowKind::Self : RowKind::Node, selectedNode_, 0);
        }
        const UpdateNode& node = tree.Node(selectedNode_);

        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::kWindow);
        ImGui::BeginChild("##updateSelection", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
        const float available = ImGui::GetContentRegionAvail().x;
        const float infoWidth = std::clamp(available * 0.25f, lineHeight * 18.0f, lineHeight * 34.0f);
        ImGui::BeginChild("##selectionInfo", ImVec2(infoWidth, 0.0f));

        ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
        ImGui::TextUnformatted("選んだ行");
        ImGui::PopStyleColor();
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.35f);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(selectedSelf_ ? (node.name + " の自分だけ").c_str() : node.name.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopFont();

        std::string context;
        if (selectedSelf_) {
            context = "子の区間のどれにも入らない時間";
        } else if (selectedNode_ != UpdateTree::kRoot) {
            context = tree.Node(node.parent).name + " の中";
        } else {
            context = "メインスレッドの、フレームごとのシーンの更新";
        }
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(context.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();

        const std::string& description = selectedSelf_ ? node.selfDescription : node.description;
        ImGui::PushTextWrapPos(0.0f);
        if (!description.empty()) {
            ImGui::TextUnformatted(description.c_str());
        }

        // 平均と最大を、そのまま文にする
        const RowValues& values = row.values;
        const bool hasMax = values.maxZone != nullptr && values.max > 0;
        std::string where;
        if (hasMax) {
            where = std::format("フレーム {}", Text::FormatCount(frames.Number(values.maxFrame)));
            if (tree.ClassOf(values.maxFrame) == FrameClass::FirstPlay) {
                where += "・再生の最初のフレーム";
            }
        }
        const char* unit = selectedSelf_ ? "フレーム" : "回";
        std::string sentence;
        if (!hasMax) {
            sentence = "数えたフレームには出てきません。";
        } else if (!selectedSelf_ && row.summary.firstOnly) {
            sentence = std::format("再生の最初のフレームにだけ動きました（{} 回）。いちばん長い 1 回は {}（{}）です。",
                Text::FormatCount(row.summary.allCalls), Ms(static_cast<double>(values.max)), where);
        } else if (values.spikes == 0) {
            sentence = std::format("平均は {}、いちばん長い 1 {}は {}（{}）です。", Ms(values.mean), unit,
                Ms(static_cast<double>(values.max)), where);
        } else if (values.spikes == 1) {
            sentence = std::format("平均は {} ですが、1 {}だけ {} かかりました（{}）。", Ms(values.mean), unit,
                Ms(static_cast<double>(values.max)), where);
        } else {
            sentence = std::format("平均は {}。1 ms 以上かかった{}が {} {}あり、いちばん長いのは {}（{}）です。", Ms(values.mean),
                unit, Text::FormatCount(values.spikes), unit, Ms(static_cast<double>(values.max)), where);
        }
        ImGui::Spacing();
        ImGui::TextUnformatted(sentence.c_str());
        ImGui::PopTextWrapPos();

        ImGui::Spacing();
        ImGui::BeginDisabled(!hasMax);
        if (Draw::AccentButton("最大のフレームを開く")) {
            OpenInTimeline(view, frames, values.maxFrame, values.maxZone, tree.MainThreadId());
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("いちばん長かったフレームを選び、タイムラインでその区間を開きます。");
        ImGui::SameLine();
        if (ImGui::Button("統計で見る")) {
            view.statsFilter = node.name;
            view.statsFilterGpu = false;
            view.statsFilterRequested = true;
            view.tab = ViewTab::Stats;
            view.tabRequested = true;
        }
        ImGui::SetItemTooltip("区間の統計の表を、この区間の名前で絞り込んで開きます。");
        infoHeight_ = ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y;
        ImGui::EndChild();

        ImGui::SameLine(0.0f, lineHeight * 1.5f);
        ImGui::BeginChild("##selectionTrend", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        DrawTrend(tree, frames, view, row);
        ImGui::EndChild();

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    void UpdateTreeView::DrawTrend(const UpdateTree& tree, const FrameSnapshot& frames, ViewState& view, const Row& row)
    {
        const float lineHeight = ImGui::GetTextLineHeight();
        const std::vector<int64_t>& values = tree.TrendValues();
        const bool ready = tree.TrendNode() == row.node && tree.TrendSelf() == (row.kind == RowKind::Self);
        const size_t first = tree.TrendFirst();
        const size_t count = ready ? values.size() : 0;

        // 縦の上限：大半のフレームが見える高さにし、飛び抜けた棒は上で切る
        std::vector<int64_t> sorted;
        sorted.reserve(count);
        for (size_t k = 0; k < count; ++k) {
            if (values[k] >= 0) {
                sorted.push_back(values[k]);
            }
        }
        std::sort(sorted.begin(), sorted.end());
        double cap = 1000.0;
        if (!sorted.empty()) {
            const double high = static_cast<double>(sorted[std::min(sorted.size() - 1, sorted.size() * 95 / 100)]);
            const double top = static_cast<double>(sorted.back());
            cap = (top <= high * 3.0) ? Draw::NiceCeil(top) : Draw::NiceCeil(high * 1.5);
        }
        cap = std::max({ cap, Draw::NiceCeil(row.values.mean * 1.25), 1000.0 });
        const bool clipped = !sorted.empty() && static_cast<double>(sorted.back()) > cap;

        ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
        const std::string caption = std::format("フレームごとの時間（表示中の {} フレーム）", Text::FormatCount(count));
        const std::string scale = std::format("縦は 0〜{}{}", ScaleMs(cap), clipped ? "。超えた棒は上で切り、いちばん長いものに数字を出す" : "");
        const float lineStart = ImGui::GetCursorPosX();
        const float lineWidth = ImGui::GetContentRegionAvail().x;
        const float scaleWidth = ImGui::CalcTextSize(scale.c_str()).x;
        ImGui::TextUnformatted(caption.c_str());
        if (ImGui::CalcTextSize(caption.c_str()).x + scaleWidth + lineHeight * 2.0f <= lineWidth) {
            ImGui::SameLine(lineStart + lineWidth - scaleWidth);
        }
        ImGui::TextUnformatted(scale.c_str());
        ImGui::PopStyleColor();

        const float legendHeight = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 size(std::max(1.0f, ImGui::GetContentRegionAvail().x), std::max(lineHeight * 4.0f, ImGui::GetContentRegionAvail().y - legendHeight));
        ImGui::InvisibleButton("##trendArea", size);
        const bool hovered = ImGui::IsItemHovered();
        const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImGui::GetColorU32(Theme::kField));
        drawList->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImGui::GetColorU32(Theme::kOutline));

        if (!ready) {
            drawList->AddText(ImVec2(pos.x + lineHeight * 0.6f, pos.y + lineHeight * 0.5f), ImGui::GetColorU32(Theme::kTextMute), "推移を写しています…");
        }

        const ImVec4 barColor = row.node != UpdateTree::kNoNode ? GroupColor(tree.Node(row.node).group) : RestColor();
        const float slot = count > 0 ? size.x / static_cast<float>(count) : size.x;
        const float bottom = pos.y + size.y - 1.0f;
        const float height = size.y - lineHeight * 1.6f;
        size_t peak = count;
        bool anyPale = false;
        drawList->PushClipRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), true);
        for (size_t k = 0; k < count; ++k) {
            const int64_t value = values[k];
            if (value < 0) {
                continue;
            }
            const bool counted = tree.IsCounted(tree.ClassOf(first + k), includeStopped_);
            const bool over = static_cast<double>(value) > cap;
            float barHeight = static_cast<float>(std::min(static_cast<double>(value), cap) / cap) * height;
            if (value > 0) {
                barHeight = std::max(barHeight, 1.0f);
            }
            const float x0 = pos.x + static_cast<float>(k) * slot;
            const float x1 = x0 + std::max(1.0f, slot >= 3.0f ? slot - 1.0f : slot);
            ImVec4 color = over ? Theme::kError : barColor;
            if (!counted) {
                color = Theme::WithAlpha(color, 0.35f);
                anyPale = true;
            }
            drawList->AddRectFilled(ImVec2(x0, bottom - barHeight), ImVec2(x1, bottom), ImGui::GetColorU32(color));
            if (over && (peak == count || value > values[peak])) {
                peak = k;
            }
        }

        // 選んだフレーム
        if (view.hasSelectedFrame && view.selectedFrame >= first && view.selectedFrame - first < count) {
            const float x0 = pos.x + static_cast<float>(view.selectedFrame - first) * slot;
            drawList->AddRect(ImVec2(x0 - 1.0f, pos.y + 1.0f), ImVec2(x0 + std::max(2.0f, slot) + 1.0f, bottom),
                ImGui::GetColorU32(Theme::WithAlpha(Theme::kText, 0.7f)));
        }

        // 平均の線
        if (row.values.mean > 0.0 && row.values.mean <= cap && count > 0) {
            const float y = bottom - static_cast<float>(row.values.mean / cap) * height;
            Draw::DashedHLine(drawList, pos.x, pos.x + size.x, y, ImGui::GetColorU32(Theme::kTextDim), 5.0f, 4.0f);
            const std::string label = "平均 " + Ms(row.values.mean);
            const ImVec2 labelSize = ImGui::CalcTextSize(label.c_str());
            const ImVec2 labelPos(pos.x + size.x - labelSize.x - lineHeight * 0.4f, y - labelSize.y - 2.0f);
            drawList->AddRectFilled(ImVec2(labelPos.x - 3.0f, labelPos.y), ImVec2(labelPos.x + labelSize.x + 3.0f, labelPos.y + labelSize.y),
                ImGui::GetColorU32(Theme::kField));
            drawList->AddText(labelPos, ImGui::GetColorU32(Theme::kTextDim), label.c_str());
        }

        // 上で切った棒のいちばん長いもの
        if (peak < count) {
            const std::string label = Ms(static_cast<double>(values[peak]));
            const ImVec2 labelSize = ImGui::CalcTextSize(label.c_str());
            const float padding = lineHeight * 0.3f;
            const float center = pos.x + (static_cast<float>(peak) + 0.5f) * slot;
            const float left = std::clamp(center - labelSize.x * 0.5f - padding, pos.x + 1.0f, pos.x + size.x - labelSize.x - padding * 2.0f - 1.0f);
            const float top = pos.y + 3.0f;
            drawList->AddRectFilled(ImVec2(left, top), ImVec2(left + labelSize.x + padding * 2.0f, top + labelSize.y + 2.0f),
                ImGui::GetColorU32(Theme::kError));
            drawList->AddText(ImVec2(left + padding, top + 1.0f), Draw::TextOn(Theme::kError), label.c_str());
        }

        // 指したフレーム
        if (hovered && count > 0) {
            const float mouseX = ImGui::GetIO().MousePos.x;
            const size_t k = std::min(count - 1, static_cast<size_t>(std::max(0.0f, (mouseX - pos.x) / slot)));
            const float x0 = pos.x + static_cast<float>(k) * slot;
            drawList->AddRectFilled(ImVec2(x0, pos.y), ImVec2(x0 + std::max(1.0f, slot), bottom),
                ImGui::GetColorU32(Theme::WithAlpha(Theme::kText, 0.12f)));
            const size_t frame = first + k;
            const FrameClass frameClass = tree.ClassOf(frame);
            if (ImGui::BeginTooltip()) {
                ImGui::Text("フレーム %s", Text::FormatCount(frames.Number(frame)).c_str());
                if (values[k] >= 0) {
                    ImGui::SameLine(0.0f, lineHeight * 0.8f);
                    ImGui::TextUnformatted(Ms(static_cast<double>(values[k])).c_str());
                }
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
                ImGui::Text("%s%s", FrameClassLabel(frameClass),
                    (frameClass != FrameClass::None && !tree.IsCounted(frameClass, includeStopped_)) ? "（平均に入れていない）" : "");
                ImGui::TextUnformatted("クリックするとこのフレームを選びます。");
                ImGui::PopStyleColor();
                ImGui::EndTooltip();
            }
            if (clicked) {
                SelectFrame(view, frames, frame);
                view.followLatest = false;
            }
        }
        drawList->PopClipRect();

        ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextMute);
        ImGui::TextUnformatted(anyPale ? "薄い棒は平均に入れていないフレーム（停止中・再生の最初）。上の帯で見る範囲を動かすと、ここも動きます。"
                                       : "上の帯で見る範囲を動かすと、ここも動きます。");
        ImGui::PopStyleColor();
    }
}
