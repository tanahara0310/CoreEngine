#pragma once

#include "Graphics/Common/GpuTimestampProfiler.h"

struct ID3D12GraphicsCommandList;

namespace CoreEngine
{
    class EngineSystem;

    /// @brief GPU/CPU プロファイル計測の RAII スコープラッパー
    /// @details USE_IMGUI 有効時は内部で GpuTimestampProfiler::ProfileScope と同等の処理を行い、
    ///　USE_IMGUI 無効時は完全な no-op となる。
    ///　これにより呼び出し側から `#ifdef USE_IMGUI` ブロックを排除できる。
    class EngineProfileScope
    {
    public:
#ifdef USE_IMGUI
        EngineProfileScope(EngineSystem* engine, GpuTimestampSlot slot, ID3D12GraphicsCommandList* cmdList);
        ~EngineProfileScope();
#else
        EngineProfileScope(EngineSystem* /*engine*/, GpuTimestampSlot /*slot*/, ID3D12GraphicsCommandList* /*cmdList*/) noexcept {}
        ~EngineProfileScope() = default;
#endif

        EngineProfileScope(const EngineProfileScope&) = delete;
        EngineProfileScope& operator=(const EngineProfileScope&) = delete;

#ifdef USE_IMGUI
    private:
        GpuTimestampProfiler* profiler_ = nullptr;
        GpuTimestampSlot slot_;
        ID3D12GraphicsCommandList* cmdList_ = nullptr;
#endif
    };
}
