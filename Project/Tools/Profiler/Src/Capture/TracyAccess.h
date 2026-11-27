#pragma once

#include "TracyWorker.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
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

    /// @brief 区間の名前（場所に名前が無ければ関数の名前）
    inline const char* SourceName(const tracy::Worker& worker, int16_t srcloc)
    {
        const tracy::SourceLocation& location = worker.GetSourceLocation(srcloc);
        return worker.GetString(location.name.active ? location.name : location.function);
    }

    /// @brief スレッドの札（受け取り役が知っている名前と番号）
    inline std::string ThreadLabel(const tracy::Worker& worker, uint64_t id)
    {
        const char* name = worker.GetThreadName(id);
        if (std::strcmp(name, "Main thread") == 0 || std::strcmp(name, "???") == 0) {
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
