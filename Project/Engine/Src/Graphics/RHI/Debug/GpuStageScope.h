#pragma once

#include "Graphics/RHI/Debug/GpuMarker.h"
#include "Graphics/RHI/Debug/GpuTimestampProfiler.h"

#include <cstdint>

struct ID3D12GraphicsCommandList;

namespace CoreEngine
{
    /// @brief PIX マーカーと CPU/GPU タイムスタンプを同じ範囲で積むスコープ
    /// @details 1 パスの内部を分解して計測する。名前ごとに動的スロットが割り当てられる。
    /// @note 同一スロットへ Begin/End を 1 フレームに複数回積むと最後の 1 回だけが残るため、
    ///       同じ処理を複数回走らせる場合は回ごとに別の名前を渡すこと。
    class GpuStageScope
    {
    public:
        /// @param profiler nullptr なら PIX マーカーだけを積む
        GpuStageScope(ID3D12GraphicsCommandList* cmdList,
            GpuTimestampProfiler* profiler,
            const char* name,
            GpuTimingCategory category)
            : cmdList_(cmdList)
            , profiler_(profiler)
        {
            BeginGpuMarker(cmdList_, name);
            if (profiler_ && cmdList_) {
                slot_ = profiler_->GetOrCreateNamedSlot(name, category);
                if (slot_ != UINT32_MAX) {
                    profiler_->BeginCpuTimestamp(slot_);
                    profiler_->BeginGpuTimestamp(slot_, cmdList_);
                }
            }
        }

        ~GpuStageScope()
        {
            if (profiler_ && slot_ != UINT32_MAX) {
                profiler_->EndGpuTimestamp(slot_, cmdList_);
                profiler_->EndCpuTimestamp(slot_);
            }
            EndGpuMarker(cmdList_);
        }

        GpuStageScope(const GpuStageScope&) = delete;
        GpuStageScope& operator=(const GpuStageScope&) = delete;

    private:
        ID3D12GraphicsCommandList* cmdList_ = nullptr;
        GpuTimestampProfiler* profiler_ = nullptr;
        uint32_t slot_ = UINT32_MAX;
    };
}
