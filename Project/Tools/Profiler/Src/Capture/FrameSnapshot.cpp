#include "Capture/FrameSnapshot.h"

#include "Capture/SourceInfoCache.h"
#include "Capture/TracyAccess.h"

#include <algorithm>
#include <cstring>

namespace CoreEngine::Profiler
{
    namespace
    {
        /// @brief 待ちを探して辿る区間の深さの上限
        constexpr int kMaxWaitDepth = 16;

        /// @brief [begin, end) に入る待ちの区間の時間を足す。待ちの区間の中へは入らない
        int64_t SumWaits(const tracy::Worker& worker, SourceInfoCache& sources,
            const tracy::Vector<tracy::short_ptr<tracy::ZoneEvent>>& events, int64_t begin, int64_t end, int depth)
        {
            int64_t sum = 0;
            for (size_t i = Access::FirstEndingAfter(worker, events, begin); i < events.size(); ++i) {
                const tracy::ZoneEvent& zone = Access::At(events, i);
                const int64_t start = zone.Start();
                if (start >= end) {
                    break;
                }
                if (sources.Get(worker, zone.SrcLoc()).kind == ZoneKind::Wait) {
                    sum += std::min(worker.GetZoneEnd(zone), end) - std::max(start, begin);
                } else if (zone.HasChildren() && depth < kMaxWaitDepth) {
                    sum += SumWaits(worker, sources, worker.GetZoneChildren(zone.Child()), begin, end, depth + 1);
                }
            }
            return sum;
        }
    }

    void FrameSnapshot::Reset()
    {
        frames_.clear();
        frameOffset_ = 0;
        mainThread_ = nullptr;
        mainThreadByName_ = false;
        live_ = false;
    }

    void FrameSnapshot::UpdateFrames(const tracy::Worker& worker, bool live)
    {
        live_ = live;
        frameOffset_ = worker.GetFrameOffset();
        if (!mainThreadByName_) {
            FindMainThread(worker);
        }

        const tracy::FrameData* base = worker.GetFramesBase();
        const size_t count = base ? worker.GetFrameCount(*base) : 0;
        if (count < frames_.size()) {
            frames_.clear();
        }
        // 前に最後だったフレームは、受け取りの最中は終わりが伸びていたので写し直す
        const size_t from = frames_.empty() ? 0 : frames_.size() - 1;
        frames_.resize(count);
        for (size_t i = from; i < count; ++i) {
            FrameTime& frame = frames_[i];
            frame.begin = worker.GetFrameBegin(*base, i);
            frame.end = worker.GetFrameEnd(*base, i);
            frame.wait = -1;
        }
    }

    void FrameSnapshot::UpdateWaits(const tracy::Worker& worker, SourceInfoCache& sources, size_t first, size_t last)
    {
        if (!mainThread_) {
            return;
        }
        last = std::min(last, frames_.size());
        for (size_t i = first; i < last; ++i) {
            FrameTime& frame = frames_[i];
            if (frame.wait >= 0 && IsComplete(i)) {
                continue;
            }
            frame.wait = SumWaits(worker, sources, mainThread_->timeline, frame.begin, frame.end, 0);
        }
    }

    uint64_t FrameSnapshot::Number(size_t index) const
    {
        return frameOffset_ == 0 ? index : index + frameOffset_ - 1;
    }

    size_t FrameSnapshot::FindFrame(int64_t time) const
    {
        if (frames_.empty()) {
            return 0;
        }
        const auto it = std::upper_bound(frames_.begin(), frames_.end(), time,
            [](int64_t value, const FrameTime& frame) { return value < frame.begin; });
        if (it == frames_.begin()) {
            return 0;
        }
        return static_cast<size_t>(it - frames_.begin()) - 1;
    }

    size_t FrameSnapshot::LastComplete() const
    {
        if (frames_.empty()) {
            return 0;
        }
        const size_t last = frames_.size() - 1;
        return (IsComplete(last) || last == 0) ? last : last - 1;
    }

    void FrameSnapshot::FindMainThread(const tracy::Worker& worker)
    {
        const tracy::ThreadData* busiest = nullptr;
        for (const tracy::ThreadData* thread : worker.GetThreadData()) {
            if (std::strcmp(worker.GetThreadName(thread->id), "Main thread") == 0) {
                mainThread_ = thread;
                mainThreadByName_ = true;
                return;
            }
            if (!busiest || thread->count > busiest->count) {
                busiest = thread;
            }
        }
        mainThread_ = busiest;
    }
}
