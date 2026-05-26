#include "pch.h"
#include "EngineProfileScope.h"

#ifdef USE_IMGUI

#include "../EngineSystem.h"
#include "DebugSubsystem.h"

namespace CoreEngine
{
    EngineProfileScope::EngineProfileScope(
        EngineSystem* engine,
        GpuTimestampSlot slot,
        ID3D12GraphicsCommandList* cmdList)
        : slot_(slot)
        , cmdList_(cmdList)
    {
        if (auto* debug = engine ? engine->GetDebugSubsystem() : nullptr) {
            profiler_ = &debug->GetGpuProfiler();
            profiler_->BeginCpuTimestamp(slot_);
            profiler_->BeginGpuTimestamp(slot_, cmdList_);
        }
    }

    EngineProfileScope::~EngineProfileScope()
    {
        if (profiler_) {
            profiler_->EndGpuTimestamp(slot_, cmdList_);
            profiler_->EndCpuTimestamp(slot_);
        }
    }
}

#endif // USE_IMGUI
