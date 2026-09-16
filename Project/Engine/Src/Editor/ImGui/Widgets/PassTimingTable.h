#pragma once

#include "Graphics/RHI/Debug/GpuTimestampProfiler.h"
#include <imgui.h>
#include <array>

/// @file
/// @brief パス別の CPU / GPU 時間の表示（しきい値と色はここだけで決める）

namespace CoreEngine::UI
{
    /// @brief 計測スロットの並び
    using TimingSlots = std::array<GpuTimingResult, GpuTimestampProfiler::kSlotCount>;

    /// @brief GPU 時間の色
    /// @param idle そのフレームで実行されなかったパスなら true（淡色になる）
    ImVec4 GpuTimeColor(float milliseconds, bool idle = false);

    /// @brief CPU 時間の色
    /// @param idle そのフレームで実行されなかったパスなら true（淡色になる）
    ImVec4 CpuTimeColor(float milliseconds, bool idle = false);

    /// @brief パス別の CPU / GPU 時間の表（カテゴリごとにまとめ、最後にフレーム合計の行を出す）
    /// @param id 表の ID
    /// @param showBudget 1 フレームの予算（60FPS）に対する割合のバーを出すか
    void PassTimingTable(const char* id, const TimingSlots& slots, bool showBudget);
}

