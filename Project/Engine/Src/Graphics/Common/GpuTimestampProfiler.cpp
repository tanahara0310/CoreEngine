#include "GpuTimestampProfiler.h"

#include <cassert>

namespace CoreEngine
{
    void GpuTimestampProfiler::Initialize(ID3D12Device* device)
    {
        if (!device) return;

        // ── クエリヒープ作成 ────────────────────────────────────
        D3D12_QUERY_HEAP_DESC heapDesc = {};
        heapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        heapDesc.Count = kQueriesPerFrame * kFrameCount;
        heapDesc.NodeMask = 0;
        HRESULT hr = device->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(&queryHeap_));
        assert(SUCCEEDED(hr));

        // ── フレームごとの readback バッファ作成 ────────────────
        const UINT64 bufferSize = static_cast<UINT64>(kQueriesPerFrame) * sizeof(uint64_t);

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_READBACK;

        D3D12_RESOURCE_DESC resDesc = {};
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resDesc.Width = bufferSize;
        resDesc.Height = 1;
        resDesc.DepthOrArraySize = 1;
        resDesc.MipLevels = 1;
        resDesc.Format = DXGI_FORMAT_UNKNOWN;
        resDesc.SampleDesc.Count = 1;
        resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        for (uint32_t i = 0; i < kFrameCount; ++i)
        {
            hr = device->CreateCommittedResource(
                &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                IID_PPV_ARGS(&readbackBuffers_[i]));
            assert(SUCCEEDED(hr));
        }

        // スロット名を初期設定
        for (uint32_t i = 0; i < kSlotCount; ++i)
        {
            lastResults_[i].name = GetSlotName(static_cast<GpuTimestampSlot>(i));
        }

        initialized_ = true;
    }

    void GpuTimestampProfiler::Finalize()
    {
        for (auto& buf : readbackBuffers_) buf.Reset();
        queryHeap_.Reset();
        initialized_ = false;
    }

    void GpuTimestampProfiler::NewFrame(uint32_t frameIndex)
    {
        currentFrameIndex_ = frameIndex % kFrameCount;
        activatedSlots_[currentFrameIndex_] = 0;
        for (uint32_t i = 0; i < kSlotCount; ++i)
        {
            cpuTimesMs_[currentFrameIndex_][i] = 0.0f;
        }
    }

    void GpuTimestampProfiler::BeginGpuTimestamp(GpuTimestampSlot slot, ID3D12GraphicsCommandList* cmdList)
    {
        if (!initialized_ || !cmdList) return;
        activatedSlots_[currentFrameIndex_] |= (1u << static_cast<uint32_t>(slot));
        const uint32_t base = currentFrameIndex_ * kQueriesPerFrame;
        const uint32_t index = static_cast<uint32_t>(slot) * kQueriesPerSlot;
        cmdList->EndQuery(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, base + index);
    }

    void GpuTimestampProfiler::EndGpuTimestamp(GpuTimestampSlot slot, ID3D12GraphicsCommandList* cmdList)
    {
        if (!initialized_ || !cmdList) return;
        const uint32_t base = currentFrameIndex_ * kQueriesPerFrame;
        const uint32_t index = static_cast<uint32_t>(slot) * kQueriesPerSlot + 1;
        cmdList->EndQuery(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, base + index);
    }

    void GpuTimestampProfiler::ResolveAll(ID3D12GraphicsCommandList* cmdList, uint32_t frameIndex)
    {
        if (!initialized_ || !cmdList) return;
        const uint32_t bufIdx = frameIndex % kFrameCount;
        const uint32_t base = bufIdx * kQueriesPerFrame;

        // 実際に BeginGpuTimestamp されたスロットのみ resolve し、
        // 未使用クエリの RESOLVE_QUERY_INVALID_QUERY_STATE を防ぐ。
        for (uint32_t i = 0; i < kSlotCount; ++i)
        {
            if (!(activatedSlots_[bufIdx] & (1u << i))) continue;

            const uint32_t firstQuery = base + i * kQueriesPerSlot;
            const UINT64 destOffset = static_cast<UINT64>(i * kQueriesPerSlot) * sizeof(uint64_t);
            cmdList->ResolveQueryData(
                queryHeap_.Get(),
                D3D12_QUERY_TYPE_TIMESTAMP,
                firstQuery, kQueriesPerSlot,
                readbackBuffers_[bufIdx].Get(), destOffset);
        }
    }

    void GpuTimestampProfiler::ReadResults(ID3D12CommandQueue* commandQueue, uint32_t readFrameIndex)
    {
        if (!initialized_ || !commandQueue) return;

        UINT64 gpuFreq = 0;
        commandQueue->GetTimestampFrequency(&gpuFreq);
        if (gpuFreq == 0) return;

        const double msPerTick = 1000.0 / static_cast<double>(gpuFreq);
        const uint32_t bufIdx = readFrameIndex % kFrameCount;
        const UINT64 bufferSize = static_cast<UINT64>(kQueriesPerFrame) * sizeof(uint64_t);
        const D3D12_RANGE readRange = { 0, bufferSize };

        void* pData = nullptr;
        HRESULT hr = readbackBuffers_[bufIdx]->Map(0, &readRange, &pData);
        if (FAILED(hr)) return;

        const uint64_t* ts = static_cast<const uint64_t*>(pData);
        for (uint32_t i = 0; i < kSlotCount; ++i)
        {
            lastResults_[i].name = GetSlotName(static_cast<GpuTimestampSlot>(i));
            lastResults_[i].cpuMs = cpuTimesMs_[bufIdx][i];

            if (activatedSlots_[bufIdx] & (1u << i)) {
                const uint64_t t0 = ts[i * kQueriesPerSlot];
                const uint64_t t1 = ts[i * kQueriesPerSlot + 1];
                lastResults_[i].gpuMs = (t1 >= t0) ? static_cast<float>((t1 - t0) * msPerTick) : 0.0f;
            } else {
                lastResults_[i].gpuMs = 0.0f;
            }
        }

        const D3D12_RANGE writeRange = { 0, 0 };
        readbackBuffers_[bufIdx]->Unmap(0, &writeRange);
    }

    void GpuTimestampProfiler::BeginCpuTimestamp(GpuTimestampSlot slot)
    {
        cpuBegin_[currentFrameIndex_][static_cast<uint32_t>(slot)] = std::chrono::high_resolution_clock::now();
    }

    void GpuTimestampProfiler::EndCpuTimestamp(GpuTimestampSlot slot)
    {
        const uint32_t idx = static_cast<uint32_t>(slot);
        cpuTimesMs_[currentFrameIndex_][idx] = std::chrono::duration<float, std::milli>(
            std::chrono::high_resolution_clock::now() - cpuBegin_[currentFrameIndex_][idx]).count();
    }
}
