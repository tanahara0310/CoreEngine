#include "pch.h"
#include "GpuTimestampProfiler.h"
#include "Graphics/Resource/ResourceFactory.h"

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
        [[maybe_unused]] HRESULT hr = device->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(&queryHeap_));
        assert(SUCCEEDED(hr));

        // ── フレームごとの readback バッファ作成 ────────────────
        const UINT64 bufferSize = static_cast<UINT64>(kQueriesPerFrame) * sizeof(uint64_t);

        for (uint32_t i = 0; i < kFrameCount; ++i)
        {
            readbackBuffers_[i] = ResourceFactory::CreateBufferResource(
                device, bufferSize, D3D12_HEAP_TYPE_READBACK);
        }

        // 固定スロットの名前・カテゴリを初期設定（動的スロットは登録時に設定される）
        lastResults_[static_cast<uint32_t>(GpuTimestampSlot::ImGuiDraw)].name = GetSlotName(static_cast<uint32_t>(GpuTimestampSlot::ImGuiDraw));
        lastResults_[static_cast<uint32_t>(GpuTimestampSlot::ImGuiDraw)].category = GpuTimingCategory::Editor;
        lastResults_[static_cast<uint32_t>(GpuTimestampSlot::Total)].name = GetSlotName(static_cast<uint32_t>(GpuTimestampSlot::Total));
        lastResults_[static_cast<uint32_t>(GpuTimestampSlot::Total)].category = GpuTimingCategory::Frame;

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
        activatedSlots_[currentFrameIndex_].fill(false);
        for (uint32_t i = 0; i < kSlotCount; ++i)
        {
            cpuTimesMs_[currentFrameIndex_][i] = 0.0f;
        }
    }

    uint32_t GpuTimestampProfiler::GetOrCreateNamedSlot(const std::string& name, GpuTimingCategory category)
    {
        if (auto it = namedSlotIndex_.find(name); it != namedSlotIndex_.end()) {
            return it->second;
        }
        if (dynamicSlotCount_ >= kMaxDynamicSlots) {
            return UINT32_MAX;
        }

        const uint32_t dynIndex = dynamicSlotCount_++;
        const uint32_t slotIndex = kFixedSlotCount + dynIndex;
        dynamicNames_[dynIndex] = name;
        dynamicCategories_[dynIndex] = category;
        namedSlotIndex_.emplace(name, slotIndex);

        lastResults_[slotIndex].name = dynamicNames_[dynIndex].c_str();
        lastResults_[slotIndex].category = category;

        return slotIndex;
    }

    void GpuTimestampProfiler::BeginGpuTimestamp(uint32_t slotIndex, ID3D12GraphicsCommandList* cmdList)
    {
        if (!initialized_ || !cmdList || slotIndex >= kSlotCount) return;
        activatedSlots_[currentFrameIndex_][slotIndex] = true;
        const uint32_t base = currentFrameIndex_ * kQueriesPerFrame;
        const uint32_t index = slotIndex * kQueriesPerSlot;
        cmdList->EndQuery(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, base + index);
    }

    void GpuTimestampProfiler::EndGpuTimestamp(uint32_t slotIndex, ID3D12GraphicsCommandList* cmdList)
    {
        if (!initialized_ || !cmdList || slotIndex >= kSlotCount) return;
        const uint32_t base = currentFrameIndex_ * kQueriesPerFrame;
        const uint32_t index = slotIndex * kQueriesPerSlot + 1;
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
            if (!activatedSlots_[bufIdx][i]) continue;

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
            lastResults_[i].cpuMs = cpuTimesMs_[bufIdx][i];

            if (activatedSlots_[bufIdx][i]) {
                const uint64_t t0 = ts[i * kQueriesPerSlot];
                const uint64_t t1 = ts[i * kQueriesPerSlot + 1];
                lastResults_[i].gpuMs = (t1 >= t0) ? static_cast<float>((t1 - t0) * msPerTick) : 0.0f;
            } else {
                lastResults_[i].gpuMs = 0.0f;
            }

            // 表示行の採否に使う単調フラグ。フレームごとの値で採否を決めると、
            // 計測値が閾値付近で揺れるパス（ほぼ空のパス）の行が出入りして
            // 表全体が上下にずれるため、一度でも有意な時間が出たら以後は出し続ける。
            if (lastResults_[i].gpuMs >= kTimingActiveThresholdMs ||
                lastResults_[i].cpuMs >= kTimingActiveThresholdMs) {
                lastResults_[i].everActive = true;
            }
        }

        const D3D12_RANGE writeRange = { 0, 0 };
        readbackBuffers_[bufIdx]->Unmap(0, &writeRange);
    }

    void GpuTimestampProfiler::BeginCpuTimestamp(uint32_t slotIndex)
    {
        if (slotIndex >= kSlotCount) return;
        cpuBegin_[currentFrameIndex_][slotIndex] = std::chrono::high_resolution_clock::now();
    }

    void GpuTimestampProfiler::EndCpuTimestamp(uint32_t slotIndex)
    {
        if (slotIndex >= kSlotCount) return;
        cpuTimesMs_[currentFrameIndex_][slotIndex] = std::chrono::duration<float, std::milli>(
            std::chrono::high_resolution_clock::now() - cpuBegin_[currentFrameIndex_][slotIndex]).count();
    }
}
