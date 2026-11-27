#include "Capture/UpdateTree.h"

#include "App/ViewState.h"
#include "Capture/FrameSnapshot.h"
#include "Capture/SourceInfoCache.h"
#include "Capture/TracyAccess.h"
#include "Text/ZoneDescriptions.h"

#include <algorithm>
#include <cstring>
#include <string_view>

namespace CoreEngine::Profiler
{
    namespace
    {
        /// @brief 1 回の Update で数えるフレームの数の上限（開いた記録は何回かに分けて数える）
        constexpr size_t kFramesPerUpdate = 2000;

        /// @brief 辿る区間の深さの上限
        constexpr uint32_t kMaxDepth = 32;

        /// @brief 「重い 1 回」とみなす時間（ナノ秒）
        constexpr int64_t kSpikeNs = 1'000'000;

        /// @brief 根の区間の名前と、再生中とみなす区間の名前
        constexpr std::string_view kRootName = "Update";
        constexpr std::string_view kPlayMarkerName = "GameObject Update";

        /// @brief シーンの更新の段の名前
        constexpr std::string_view kPhaseNames[] = {
            "FrameStart", "PreObjectUpdate", "BetweenObjectUpdates", "PostObjectUpdate", "PostLogic",
        };

        constexpr size_t Index(FrameClass frameClass)
        {
            return static_cast<size_t>(frameClass);
        }

        uint64_t ChildKey(uint32_t parent, int16_t srcloc)
        {
            return (static_cast<uint64_t>(parent) << 16) | static_cast<uint16_t>(srcloc);
        }

        UpdateNodeGroup GroupOf(std::string_view name, ZoneKind kind)
        {
            if (kind == ZoneKind::Script) {
                return UpdateNodeGroup::Script;
            }
            if (std::find(std::begin(kPhaseNames), std::end(kPhaseNames), name) != std::end(kPhaseNames)) {
                return UpdateNodeGroup::Phase;
            }
            if (name.ends_with("Feature")) {
                return UpdateNodeGroup::Feature;
            }
            if (kind == ZoneKind::Update) {
                return UpdateNodeGroup::Object;
            }
            return UpdateNodeGroup::Other;
        }

        int64_t ZoneTime(const tracy::Worker& worker, const tracy::ZoneEvent& zone)
        {
            return worker.GetZoneEnd(zone) - zone.Start();
        }

        /// @brief 区間の木を辿り、節の分かる区間の時間を節ごとに足す。並びの区間の時間の和を返す
        int64_t AddKnown(const tracy::Worker& worker, const tracy::Vector<tracy::short_ptr<tracy::ZoneEvent>>& events, uint32_t parent,
            uint32_t depth, const std::unordered_map<uint64_t, uint32_t>& childMap, std::vector<int64_t>& time, std::vector<int64_t>& child)
        {
            int64_t sum = 0;
            for (size_t i = 0; i < events.size(); ++i) {
                const tracy::ZoneEvent& zone = Access::At(events, i);
                const int64_t duration = ZoneTime(worker, zone);
                sum += duration;
                const auto it = childMap.find(ChildKey(parent, zone.SrcLoc()));
                if (it == childMap.end() || it->second >= time.size()) {
                    continue;
                }
                const uint32_t node = it->second;
                time[node] += duration;
                if (zone.HasChildren() && depth < kMaxDepth) {
                    child[node] += AddKnown(worker, worker.GetZoneChildren(zone.Child()), node, depth + 1, childMap, time, child);
                }
            }
            return sum;
        }
    }

    void UpdateTree::Reset()
    {
        *this = UpdateTree{};
    }

    void UpdateTree::Update(const tracy::Worker& worker, const FrameSnapshot& frames, SourceInfoCache& sources, const ViewState& view)
    {
        const tracy::ThreadData* thread = frames.MainThread();
        if (!thread) {
            return;
        }
        size_t available = frames.Count();
        if (available > 0 && !frames.IsComplete(available - 1)) {
            --available;
        }
        if (thread != mainThread_ || available < frameClass_.size()) {
            Reset();
            mainThread_ = thread;
        }
        mainThreadId_ = thread->id;
        available_ = available;

        const size_t stop = std::min(available, frameClass_.size() + kFramesPerUpdate);
        for (size_t i = frameClass_.size(); i < stop; ++i) {
            const FrameTime& frame = frames.At(i);
            ProcessFrame(worker, sources, *thread, i, frame.begin, frame.end);
        }
        if (orderDirty_) {
            SortChildren();
            orderDirty_ = false;
        }

        UpdateSelected(worker, view);
        UpdateTrend(worker, frames, view);
    }

    const tracy::ZoneEvent* UpdateTree::FindRoot(const tracy::Worker& worker, SourceInfoCache& sources, const tracy::ThreadData& thread,
        int64_t begin, int64_t end)
    {
        const auto& timeline = thread.timeline;
        for (size_t i = Access::FirstEndingAfter(worker, timeline, begin); i < timeline.size(); ++i) {
            const tracy::ZoneEvent& zone = Access::At(timeline, i);
            const int64_t start = zone.Start();
            if (start >= end) {
                break;
            }
            if (start < begin) {
                continue;
            }
            const int16_t srcloc = zone.SrcLoc();
            if (hasRootSrcloc_) {
                if (srcloc == rootSrcloc_) {
                    return &zone;
                }
                continue;
            }
            const SourceInfo& info = sources.Get(worker, srcloc);
            if (info.kind == ZoneKind::Update && kRootName == info.name) {
                hasRootSrcloc_ = true;
                rootSrcloc_ = srcloc;
                return &zone;
            }
        }
        return nullptr;
    }

    uint32_t UpdateTree::AddNode(const tracy::Worker& worker, SourceInfoCache& sources, uint32_t parent, int16_t srcloc)
    {
        const SourceInfo& info = sources.Get(worker, srcloc);
        const uint32_t index = static_cast<uint32_t>(nodes_.size());
        UpdateNode node;
        node.name = info.name;
        node.description = ZoneDescriptions::Instance().Describe(node.name, info.file);
        node.selfDescription = ZoneDescriptions::Instance().DescribeSelf(node.name);
        node.parent = nodes_.empty() ? 0 : parent;
        node.depth = nodes_.empty() ? 0 : nodes_[parent].depth + 1;
        node.group = nodes_.empty() ? UpdateNodeGroup::Root : GroupOf(node.name, info.kind);
        node.playMarker = node.name == kPlayMarkerName;
        nodes_.push_back(std::move(node));
        scratch_.emplace_back();
        if (index != kRoot) {
            nodes_[parent].children.push_back(index);
        }
        return index;
    }

    uint32_t UpdateTree::ChildOf(const tracy::Worker& worker, SourceInfoCache& sources, uint32_t parent, int16_t srcloc)
    {
        const uint64_t key = ChildKey(parent, srcloc);
        if (const auto it = childMap_.find(key); it != childMap_.end()) {
            return it->second;
        }
        // 場所が違っても名前が同じなら同じ節にまとめる
        const char* name = sources.Get(worker, srcloc).name;
        for (const uint32_t child : nodes_[parent].children) {
            if (nodes_[child].name == name) {
                childMap_.emplace(key, child);
                return child;
            }
        }
        const uint32_t child = AddNode(worker, sources, parent, srcloc);
        childMap_.emplace(key, child);
        return child;
    }

    uint32_t UpdateTree::FindChild(uint32_t parent, int16_t srcloc) const
    {
        const auto it = childMap_.find(ChildKey(parent, srcloc));
        return it == childMap_.end() ? kNoNode : it->second;
    }

    void UpdateTree::Touch(uint32_t node, const tracy::ZoneEvent& zone, int64_t time, int64_t offset)
    {
        Scratch& scratch = scratch_[node];
        if (!scratch.touched) {
            scratch.touched = true;
            scratch.firstOffset = offset;
            touched_.push_back(node);
        }
        scratch.time += time;
        ++scratch.calls;
        if (time >= kSpikeNs) {
            ++scratch.spikes;
        }
        if (time > scratch.max) {
            scratch.max = time;
            scratch.maxZone = &zone;
        }
    }

    void UpdateTree::ProcessFrame(const tracy::Worker& worker, SourceInfoCache& sources, const tracy::ThreadData& thread,
        size_t index, int64_t begin, int64_t end)
    {
        const tracy::ZoneEvent* root = FindRoot(worker, sources, thread, begin, end);
        if (!root) {
            frameClass_.push_back(FrameClass::None);
            frameRoot_.push_back(nullptr);
            rootTime_.push_back(-1);
            return;
        }
        if (nodes_.empty()) {
            AddNode(worker, sources, kRoot, root->SrcLoc());
        }

        // 区間の木を深さ優先で辿り、節ごとに足す（子の時間は辿り終えてから親へ足す）
        struct Pending
        {
            const tracy::ZoneEvent* zone;
            uint32_t node;
            uint32_t depth;
        };
        Touch(kRoot, *root, ZoneTime(worker, *root), 0);
        std::vector<Pending> stack;
        stack.push_back(Pending{ root, kRoot, 0 });
        while (!stack.empty()) {
            const Pending current = stack.back();
            stack.pop_back();
            if (!current.zone->HasChildren() || current.depth >= kMaxDepth) {
                continue;
            }
            const auto& children = worker.GetZoneChildren(current.zone->Child());
            const int64_t parentStart = current.zone->Start();
            int64_t childSum = 0;
            for (size_t i = 0; i < children.size(); ++i) {
                const tracy::ZoneEvent& zone = Access::At(children, i);
                const int64_t time = ZoneTime(worker, zone);
                childSum += time;
                const uint32_t node = ChildOf(worker, sources, current.node, zone.SrcLoc());
                Touch(node, zone, time, zone.Start() - parentStart);
                stack.push_back(Pending{ &zone, node, current.depth + 1 });
            }
            scratch_[current.node].child += childSum;
        }

        bool play = false;
        for (const uint32_t node : touched_) {
            play = play || nodes_[node].playMarker;
        }
        FrameClass frameClass = FrameClass::Stopped;
        if (play) {
            frameClass = (lastClass_ == FrameClass::Play || lastClass_ == FrameClass::FirstPlay) ? FrameClass::Play : FrameClass::FirstPlay;
        }
        lastClass_ = frameClass;

        for (const uint32_t node : touched_) {
            Scratch& scratch = scratch_[node];
            UpdateNode& target = nodes_[node];
            UpdateNodeStats& stats = target.stats[Index(frameClass)];
            stats.total += scratch.time;
            stats.childTotal += scratch.child;
            stats.calls += scratch.calls;
            ++stats.frames;
            stats.spikes += scratch.spikes;
            if (scratch.max > stats.max) {
                stats.max = scratch.max;
                stats.maxZone = scratch.maxZone;
                stats.maxFrame = index;
            }
            const int64_t self = scratch.time - scratch.child;
            if (self >= kSpikeNs) {
                ++stats.selfSpikes;
            }
            if (self > stats.maxSelf) {
                stats.maxSelf = self;
                stats.maxSelfZone = scratch.maxZone;
                stats.maxSelfFrame = index;
            }
            target.offsetSum += static_cast<double>(scratch.firstOffset);
            ++target.offsetCount;
            scratch = Scratch{};
        }
        touched_.clear();

        ++frameCount_[Index(frameClass)];
        if (frameClass == FrameClass::FirstPlay) {
            firstPlayFrames_.push_back(index);
        }
        frameClass_.push_back(frameClass);
        frameRoot_.push_back(root);
        rootTime_.push_back(ZoneTime(worker, *root));
        orderDirty_ = true;
    }

    void UpdateTree::SortChildren()
    {
        const auto meanOffset = [this](uint32_t node) {
            const UpdateNode& target = nodes_[node];
            return target.offsetCount > 0 ? target.offsetSum / static_cast<double>(target.offsetCount) : 0.0;
        };
        for (UpdateNode& node : nodes_) {
            std::stable_sort(node.children.begin(), node.children.end(),
                [&](uint32_t a, uint32_t b) { return meanOffset(a) < meanOffset(b); });
        }
    }

    void UpdateTree::UpdateSelected(const tracy::Worker& worker, const ViewState& view)
    {
        if (!view.hasSelectedFrame || view.selectedFrame >= frameClass_.size()) {
            selectedValid_ = false;
            return;
        }
        if (selectedValid_ && selectedFrame_ == view.selectedFrame && selectedNodeCount_ == nodes_.size()) {
            return;
        }
        selectedValid_ = true;
        selectedFrame_ = view.selectedFrame;
        selectedNodeCount_ = nodes_.size();
        selectedTime_.assign(nodes_.size(), 0);
        selectedChild_.assign(nodes_.size(), 0);
        const tracy::ZoneEvent* root = frameRoot_[selectedFrame_];
        if (!root || nodes_.empty()) {
            return;
        }
        selectedTime_[kRoot] = ZoneTime(worker, *root);
        if (root->HasChildren()) {
            selectedChild_[kRoot] = AddKnown(worker, worker.GetZoneChildren(root->Child()), kRoot, 1, childMap_, selectedTime_, selectedChild_);
        }
    }

    void UpdateTree::RequestTrend(uint32_t node, bool self)
    {
        wantTrendNode_ = node;
        wantTrendSelf_ = self;
    }

    void UpdateTree::UpdateTrend(const tracy::Worker& worker, const FrameSnapshot& frames, const ViewState& view)
    {
        if (wantTrendNode_ == kNoNode || wantTrendNode_ >= nodes_.size()) {
            trendNode_ = kNoNode;
            trendValues_.clear();
            return;
        }
        if (wantTrendNode_ != trendNode_ || wantTrendSelf_ != trendSelf_) {
            trendNode_ = wantTrendNode_;
            trendSelf_ = wantTrendSelf_;
            trendValues_.clear();
            trendPath_.clear();
            for (uint32_t node = trendNode_;; node = nodes_[node].parent) {
                trendPath_.push_back(node);
                if (node == kRoot) {
                    break;
                }
            }
            std::reverse(trendPath_.begin(), trendPath_.end());
        }

        const size_t first = view.stripFirst;
        const size_t count = frames.Count() > first ? std::min(kStripFrames, frames.Count() - first) : 0;
        std::vector<int64_t> values(count, -1);
        for (size_t k = 0; k < count; ++k) {
            const size_t frame = first + k;
            if (frame >= trendFirst_ && frame - trendFirst_ < trendValues_.size()) {
                values[k] = trendValues_[frame - trendFirst_];
            }
            if (values[k] < 0 && frame < frameClass_.size()) {
                values[k] = PathValue(worker, frame);
            }
        }
        trendFirst_ = first;
        trendValues_ = std::move(values);
    }

    int64_t UpdateTree::PathValue(const tracy::Worker& worker, size_t frame) const
    {
        const tracy::ZoneEvent* root = frameRoot_[frame];
        if (!root || trendPath_.empty()) {
            return -1;
        }
        std::vector<const tracy::ZoneEvent*> current{ root };
        std::vector<const tracy::ZoneEvent*> next;
        for (size_t level = 1; level < trendPath_.size() && !current.empty(); ++level) {
            next.clear();
            for (const tracy::ZoneEvent* zone : current) {
                if (!zone->HasChildren()) {
                    continue;
                }
                const auto& children = worker.GetZoneChildren(zone->Child());
                for (size_t i = 0; i < children.size(); ++i) {
                    const tracy::ZoneEvent& child = Access::At(children, i);
                    if (FindChild(trendPath_[level - 1], child.SrcLoc()) == trendPath_[level]) {
                        next.push_back(&child);
                    }
                }
            }
            current.swap(next);
        }
        int64_t time = 0;
        int64_t childTime = 0;
        for (const tracy::ZoneEvent* zone : current) {
            time += ZoneTime(worker, *zone);
            if (trendSelf_ && zone->HasChildren()) {
                const auto& children = worker.GetZoneChildren(zone->Child());
                for (size_t i = 0; i < children.size(); ++i) {
                    childTime += ZoneTime(worker, Access::At(children, i));
                }
            }
        }
        return trendSelf_ ? time - childTime : time;
    }

    bool UpdateTree::HasPlayFrames() const
    {
        return FrameCount(FrameClass::Play) + FrameCount(FrameClass::FirstPlay) > 0;
    }

    bool UpdateTree::IsCounted(FrameClass frameClass, bool includeStopped) const
    {
        if (!HasPlayFrames()) {
            return frameClass == FrameClass::Stopped;
        }
        return frameClass == FrameClass::Play || (includeStopped && frameClass == FrameClass::Stopped);
    }

    uint64_t UpdateTree::CountedFrames(bool includeStopped) const
    {
        uint64_t count = 0;
        for (const FrameClass frameClass : { FrameClass::Stopped, FrameClass::Play }) {
            if (IsCounted(frameClass, includeStopped)) {
                count += FrameCount(frameClass);
            }
        }
        return count;
    }

    UpdateNodeSummary UpdateTree::Summarize(uint32_t node, bool includeStopped) const
    {
        UpdateNodeSummary summary;
        if (node >= nodes_.size()) {
            return summary;
        }
        const UpdateNode& target = nodes_[node];
        int64_t total = 0;
        int64_t childTotal = 0;
        uint64_t calls = 0;
        for (const FrameClass frameClass : { FrameClass::Stopped, FrameClass::Play, FrameClass::FirstPlay }) {
            const bool counted = IsCounted(frameClass, includeStopped);
            if (!counted && frameClass != FrameClass::FirstPlay) {
                continue;
            }
            const UpdateNodeStats& stats = target.stats[Index(frameClass)];
            summary.allCalls += stats.calls;
            summary.spikes += stats.spikes;
            summary.selfSpikes += stats.selfSpikes;
            if (stats.max > summary.max) {
                summary.max = stats.max;
                summary.maxZone = stats.maxZone;
                summary.maxFrame = stats.maxFrame;
            }
            if (stats.maxSelf > summary.maxSelf) {
                summary.maxSelf = stats.maxSelf;
                summary.maxSelfZone = stats.maxSelfZone;
                summary.maxSelfFrame = stats.maxSelfFrame;
            }
            if (counted) {
                total += stats.total;
                childTotal += stats.childTotal;
                calls += stats.calls;
            }
        }
        summary.present = summary.allCalls > 0;
        summary.firstOnly = summary.present && calls == 0;
        const uint64_t frames = CountedFrames(includeStopped);
        if (frames > 0) {
            const double divisor = static_cast<double>(frames);
            summary.mean = static_cast<double>(total) / divisor;
            summary.selfMean = static_cast<double>(total - childTotal) / divisor;
            summary.callsPerFrame = static_cast<double>(calls) / divisor;
        }
        return summary;
    }

    FrameClass UpdateTree::ClassOf(size_t frame) const
    {
        return frame < frameClass_.size() ? frameClass_[frame] : FrameClass::None;
    }

    int64_t UpdateTree::RootTime(size_t frame) const
    {
        return frame < rootTime_.size() ? rootTime_[frame] : -1;
    }

    int64_t UpdateTree::SelectedTime(uint32_t node) const
    {
        return selectedValid_ && node < selectedTime_.size() ? selectedTime_[node] : -1;
    }

    int64_t UpdateTree::SelectedSelf(uint32_t node) const
    {
        return selectedValid_ && node < selectedTime_.size() ? selectedTime_[node] - selectedChild_[node] : -1;
    }
}
