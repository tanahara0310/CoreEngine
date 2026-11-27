#pragma once

#include "Capture/FrameSnapshot.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

namespace tracy
{
    struct ZoneEvent;
    struct GpuEvent;
}

/// @file ViewState.h
/// @brief フレームの帯・タイムライン・選んだ区間の欄が共有する、選んだものと見ている範囲

namespace CoreEngine::Profiler
{
    /// @brief フレームの帯に並べるフレームの数
    inline constexpr size_t kStripFrames = 300;

    /// @brief 選んだ区間（CPU か GPU のどちらか 1 つ）
    struct ZoneRef
    {
        const tracy::ZoneEvent* cpu = nullptr;
        const tracy::GpuEvent* gpu = nullptr;
        uint64_t thread = 0; ///< CPU はスレッドの番号、GPU は GPU のキューの番号（受け取り役の GetGpuData の順）

        bool IsValid() const { return cpu != nullptr || gpu != nullptr; }
        bool operator==(const ZoneRef& other) const = default;
    };

    /// @brief 下の段のタブ
    enum class ViewTab
    {
        Timeline,
        UpdateTree,
        Stats,
        Log,
    };

    /// @brief フレームの時間と目標の比べ方
    enum class FrameLevel
    {
        Normal, ///< 目標の中
        Over,   ///< 目標を超えた
        Far,    ///< 目標の 2 倍を超えた
    };

    struct ViewState
    {
        int targetFps = 60;

        bool initialized = false;   ///< 記録を開いてから最初の選び方を済ませたか
        bool hasSelectedFrame = false;
        size_t selectedFrame = 0;
        size_t stripFirst = 0;      ///< フレームの帯の左端のフレーム
        bool followLatest = false;  ///< 受け取りの最中に、最新のフレームを選び続ける

        int64_t viewStart = 0;      ///< タイムラインに出している範囲（ナノ秒）
        int64_t viewEnd = 0;

        ZoneRef selectedZone;

        ViewTab tab = ViewTab::Timeline;
        bool tabRequested = false;  ///< tab へ切り替える
        std::string statsFilter;    ///< 区間の統計の絞り込みへ入れる文字
        bool statsFilterGpu = false; ///< 区間の統計を GPU の表にする
        bool statsFilterRequested = false;

        int64_t TargetNs() const { return 1'000'000'000LL / targetFps; }
    };

    /// @brief 目標を少し超えただけのフレーム（垂直同期の揺れ）は目標の中に数える
    inline FrameLevel FrameLevelOf(int64_t duration, int64_t target)
    {
        const int64_t limit = target + target / 20;
        if (duration > limit * 2) {
            return FrameLevel::Far;
        }
        return duration > limit ? FrameLevel::Over : FrameLevel::Normal;
    }

    /// @brief フレームの帯の左端に置ける最後のフレーム
    inline size_t MaxStripFirst(const FrameSnapshot& frames)
    {
        return frames.Count() > kStripFrames ? frames.Count() - kStripFrames : 0;
    }

    /// @brief フレームを選び、タイムラインをそのフレームの範囲にし、帯をそのフレームが見える所へ動かす
    inline void SelectFrame(ViewState& view, const FrameSnapshot& frames, size_t index)
    {
        if (frames.Count() == 0) {
            return;
        }
        index = std::min(index, frames.Count() - 1);
        view.selectedFrame = index;
        view.hasSelectedFrame = true;
        const FrameTime& frame = frames.At(index);
        view.viewStart = frame.begin;
        view.viewEnd = std::max(frame.end, frame.begin + 1000);
        if (index < view.stripFirst) {
            view.stripFirst = index;
        } else if (index >= view.stripFirst + kStripFrames) {
            view.stripFirst = index + 1 - kStripFrames;
        }
        view.stripFirst = std::min(view.stripFirst, MaxStripFirst(frames));
    }
}
