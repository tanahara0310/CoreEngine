#pragma once

#include "TracyWorker.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <limits>
#include <string>

/// @file TracyAccess.h
/// @brief 受け取り役の区間の並びを、中身を直に持つ形（開いた記録）と指し示す形（受け取り中）のどちらでも同じに辿る

namespace CoreEngine::Profiler::Access
{
    /// @brief 並びの i 番目の区間
    template<class Event>
    const Event& At(const tracy::Vector<tracy::short_ptr<Event>>& events, size_t index)
    {
        if (events.is_magic()) {
            return (*reinterpret_cast<const tracy::Vector<Event>*>(&events))[index];
        }
        return *events[index];
    }

    /// @brief CPU の区間の並びで、time より後に終わる最初の区間の番号（無ければ並びの長さ）
    inline size_t FirstEndingAfter(const tracy::Worker& worker, const tracy::Vector<tracy::short_ptr<tracy::ZoneEvent>>& events, int64_t time)
    {
        size_t low = 0;
        size_t high = events.size();
        while (low < high) {
            const size_t mid = low + (high - low) / 2;
            if (At(events, mid).Start() < time) {
                low = mid + 1;
            } else {
                high = mid;
            }
        }
        if (low > 0 && worker.GetZoneEnd(At(events, low - 1)) > time) {
            --low;
        }
        return low;
    }

    /// @brief GPU の区間の並びで、time より後に終わる最初の区間の番号（終わりの分からない区間は最後に並ぶとみなす）
    inline size_t FirstGpuEndingAfter(const tracy::Vector<tracy::short_ptr<tracy::GpuEvent>>& events, int64_t time)
    {
        size_t low = 0;
        size_t high = events.size();
        while (low < high) {
            const size_t mid = low + (high - low) / 2;
            if (static_cast<uint64_t>(At(events, mid).GpuEnd()) <= static_cast<uint64_t>(time)) {
                low = mid + 1;
            } else {
                high = mid;
            }
        }
        return low;
    }

    /// @brief 区間を探して辿る深さの上限
    inline constexpr int kMaxSearchDepth = 16;

    /// @brief [begin, end) に始まる、場所が srcloc の最初の CPU の区間（子の区間の中も探す）
    inline const tracy::ZoneEvent* FindCpu(const tracy::Worker& worker, const tracy::Vector<tracy::short_ptr<tracy::ZoneEvent>>& events,
        int16_t srcloc, int64_t begin, int64_t end, int depth = 0)
    {
        for (size_t i = FirstEndingAfter(worker, events, begin); i < events.size(); ++i) {
            const tracy::ZoneEvent& event = At(events, i);
            const int64_t start = event.Start();
            if (start >= end) {
                break;
            }
            if (event.SrcLoc() == srcloc && start >= begin) {
                return &event;
            }
            if (event.HasChildren() && depth < kMaxSearchDepth) {
                if (const tracy::ZoneEvent* found = FindCpu(worker, worker.GetZoneChildren(event.Child()), srcloc, begin, end, depth + 1)) {
                    return found;
                }
            }
        }
        return nullptr;
    }

    /// @brief [begin, end) に始まる、場所が srcloc の最初の GPU の区間（子の区間の中も探す）
    inline const tracy::GpuEvent* FindGpu(const tracy::Worker& worker, const tracy::Vector<tracy::short_ptr<tracy::GpuEvent>>& events,
        int16_t srcloc, int64_t begin, int64_t end, int depth = 0)
    {
        for (size_t i = FirstGpuEndingAfter(events, begin); i < events.size(); ++i) {
            const tracy::GpuEvent& event = At(events, i);
            const int64_t start = event.GpuStart();
            if (start < 0) {
                continue;
            }
            if (start >= end) {
                break;
            }
            if (event.SrcLoc() == srcloc && start >= begin) {
                return &event;
            }
            if (event.Child() >= 0 && depth < kMaxSearchDepth) {
                if (const tracy::GpuEvent* found = FindGpu(worker, worker.GetGpuChildren(event.Child()), srcloc, begin, end, depth + 1)) {
                    return found;
                }
            }
        }
        return nullptr;
    }

    /// @brief 区間の名前（場所に名前が無ければ関数の名前）
    inline const char* SourceName(const tracy::Worker& worker, int16_t srcloc)
    {
        const tracy::SourceLocation& location = worker.GetSourceLocation(srcloc);
        return worker.GetString(location.name.active ? location.name : location.function);
    }

    /// @brief 区間の場所の名前・関数・ファイルの文字が受け取り役へ届き終えたか（届く前の文字は「???」）
    inline bool SourceArrived(const tracy::Worker& worker, int16_t srcloc)
    {
        if (srcloc < 0 || srcloc == std::numeric_limits<int16_t>::max()) {
            return true;
        }
        const tracy::SourceLocation& location = worker.GetSourceLocation(srcloc);
        if (!location.function.active) {
            return false;
        }
        const auto arrived = [&worker](const tracy::StringRef& ref) {
            return !ref.active || ref.isidx || ref.str == 0 || std::strcmp(worker.GetString(ref), "???") != 0;
        };
        return arrived(location.name) && arrived(location.function) && arrived(location.file);
    }

    /// @brief スレッドの札（受け取り役が知っている名前と番号）
    inline std::string ThreadLabel(const tracy::Worker& worker, uint64_t id)
    {
        const char* name = worker.GetThreadName(id);
        if (name[0] == '\0' || std::strcmp(name, "Main thread") == 0 || std::strcmp(name, "???") == 0) {
            return std::format("スレッド {}", id);
        }
        return std::format("{}（{}）", name, id);
    }

    /// @brief GPU のキューの札
    inline std::string QueueLabel(const tracy::Worker& worker, const tracy::GpuCtxData& context)
    {
        if (!context.name.Active()) {
            return "キュー";
        }
        const std::string name = worker.GetString(context.name);
        if (name == "Direct Queue") {
            return "描画のキュー";
        }
        if (name == "Compute Queue") {
            return "計算のキュー";
        }
        if (name == "Copy Queue") {
            return "コピーのキュー";
        }
        return name;
    }
}
