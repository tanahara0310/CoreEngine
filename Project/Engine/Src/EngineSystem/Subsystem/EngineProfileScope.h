#pragma once

#include "Graphics/RHI/Debug/GpuTimestampProfiler.h"

struct ID3D12GraphicsCommandList;

namespace CoreEngine
{
    class EngineSystem;

    /// @brief GPU/CPU プロファイル計測の RAII スコープラッパー
    /// @details CORE_EDITOR 有効時は内部で GpuTimestampProfiler::ProfileScope と同等の処理を行い、
    ///　CORE_EDITOR 無効時は完全な no-op となる。
    ///　これにより呼び出し側から `#ifdef CORE_EDITOR` ブロックを排除できる。
    class EngineProfileScope
    {
    public:
#ifdef CORE_EDITOR
        EngineProfileScope(EngineSystem* engine, GpuTimestampSlot slot, ID3D12GraphicsCommandList* cmdList);
        ~EngineProfileScope();
#else
        EngineProfileScope(EngineSystem* /*engine*/, GpuTimestampSlot /*slot*/, ID3D12GraphicsCommandList* /*cmdList*/) noexcept {}
        ~EngineProfileScope() = default;
#endif

        EngineProfileScope(const EngineProfileScope&) = delete;
        EngineProfileScope& operator=(const EngineProfileScope&) = delete;

#ifdef CORE_EDITOR
    private:
        GpuTimestampProfiler* profiler_ = nullptr;
        GpuTimestampSlot slot_;
        ID3D12GraphicsCommandList* cmdList_ = nullptr;
#endif
    };
}
