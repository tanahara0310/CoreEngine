#include "pch.h"
#include "Graphics/RHI/Debug/GpuProfileZone.h"

#include "Utility/Profiler/ProfileZone.h"

#if CORE_TRACY_ENABLED

#pragma warning(push)
#pragma warning(disable : 4018 4100 4127 4189 4244 4245 4267 4324 4456 4457 4458 4459 4505 4702 4706 4996)
#include "Externals/tracy/public/tracy/TracyD3D12.hpp"
#pragma warning(pop)

#include <cstring>
#include <new>

namespace CoreEngine
{
    namespace
    {
        tracy::D3D12QueueCtx* g_queueContext = nullptr;

        constexpr char kQueueName[] = "Direct Queue";
    }

    static_assert(sizeof(tracy::D3D12ZoneScope) <= GpuProfileZone::kStorageSize, "GpuProfileZone::kStorageSize を広げる");
    static_assert(alignof(tracy::D3D12ZoneScope) <= alignof(std::uint64_t), "GpuProfileZone::storage_ の揃えを広げる");

    void GpuProfileZone::Initialize(ID3D12Device* device, ID3D12CommandQueue* queue)
    {
        if (g_queueContext || !device || !queue) {
            return;
        }
        g_queueContext = TracyD3D12Context(device, queue);
        TracyD3D12ContextName(g_queueContext, kQueueName, static_cast<uint16_t>(sizeof(kQueueName) - 1));
    }

    void GpuProfileZone::Shutdown()
    {
        if (!g_queueContext) {
            return;
        }
        TracyD3D12Destroy(g_queueContext);
        g_queueContext = nullptr;
    }

    void GpuProfileZone::Collect()
    {
        if (g_queueContext) {
            TracyD3D12Collect(g_queueContext);
        }
    }

    GpuProfileZone::GpuProfileZone(ID3D12GraphicsCommandList* cmdList, const char* name, std::source_location location)
    {
        if (!g_queueContext || !cmdList || !name || !TracyIsConnected) {
            return;
        }
        const char* file = location.file_name();
        const char* function = location.function_name();
        new (storage_) tracy::D3D12ZoneScope(g_queueContext,
            location.line(), file, std::strlen(file), function, std::strlen(function),
            name, std::strlen(name), cmdList, true);
        active_ = true;
    }

    GpuProfileZone::~GpuProfileZone()
    {
        if (active_) {
            std::launder(reinterpret_cast<tracy::D3D12ZoneScope*>(storage_))->~D3D12ZoneScope();
        }
    }
}

#else

namespace CoreEngine
{
    void GpuProfileZone::Initialize(ID3D12Device*, ID3D12CommandQueue*) {}
    void GpuProfileZone::Shutdown() {}
    void GpuProfileZone::Collect() {}

    GpuProfileZone::GpuProfileZone(ID3D12GraphicsCommandList*, const char*, std::source_location) {}
    GpuProfileZone::~GpuProfileZone() = default;
}

#endif
