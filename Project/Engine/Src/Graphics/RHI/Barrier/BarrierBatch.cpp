#include "pch.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"

#include "Utility/Logger/Logger.h"

#include <cassert>
#include <string>

namespace CoreEngine
{
    namespace
    {
        /// バリアの逐次ログ。既定 OFF（Debug で全部出すと 60 秒 100MB を超える）
        bool g_barrierLoggingEnabled = false;

        /// @brief D3D12_RESOURCE_STATES を人間が読める文字列に変換する（デバッグログ用）
        std::string ResourceStateToString(D3D12_RESOURCE_STATES state)
        {
            if (state == D3D12_RESOURCE_STATE_COMMON)                            return "COMMON";
            if (state == D3D12_RESOURCE_STATE_RENDER_TARGET)                     return "RENDER_TARGET";
            if (state == D3D12_RESOURCE_STATE_UNORDERED_ACCESS)                  return "UNORDERED_ACCESS";
            if (state == D3D12_RESOURCE_STATE_DEPTH_WRITE)                       return "DEPTH_WRITE";
            if (state == D3D12_RESOURCE_STATE_DEPTH_READ)                        return "DEPTH_READ";
            if (state == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)             return "PIXEL_SHADER_RESOURCE";
            if (state == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)         return "NON_PIXEL_SHADER_RESOURCE";
            if (state == D3D12_RESOURCE_STATE_COPY_SOURCE)                       return "COPY_SOURCE";
            if (state == D3D12_RESOURCE_STATE_COPY_DEST)                         return "COPY_DEST";
            if (state == D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT)                 return "INDIRECT_ARGUMENT";
            if (state == D3D12_RESOURCE_STATE_GENERIC_READ)                      return "GENERIC_READ";
            if (state == D3D12_RESOURCE_STATE_PRESENT)                           return "PRESENT/COMMON";
            if (state == D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE) return "RAYTRACING_ACCELERATION_STRUCTURE";
            if (state == (D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE))
                return "DEPTH_READ|PIXEL_SHADER_RESOURCE";
            return "UNKNOWN(0x" + std::to_string(static_cast<UINT>(state)) + ")";
        }

        void LogTransition(ID3D12Resource* resource,
            D3D12_RESOURCE_STATES before,
            D3D12_RESOURCE_STATES after,
            uint32_t subresource)
        {
            if (!g_barrierLoggingEnabled) {
                return;
            }
            Logger::GetInstance().Logf(
                LogLevel::Debug, LogCategory::Graphics, LogSubCategory::Barrier,
                "[Barrier] resource=0x{:X} sub={} {} -> {}",
                reinterpret_cast<uintptr_t>(resource),
                subresource == kAllSubresources ? -1 : static_cast<int32_t>(subresource),
                ResourceStateToString(before),
                ResourceStateToString(after));
        }
    }

    void BarrierBatch::SetLoggingEnabled(bool enabled) noexcept
    {
        g_barrierLoggingEnabled = enabled;
    }

    bool BarrierBatch::IsLoggingEnabled() noexcept
    {
        return g_barrierLoggingEnabled;
    }

    BarrierBatch::BarrierBatch(ID3D12GraphicsCommandList* cmdList)
        : cmdList_(cmdList)
    {
        assert(cmdList_ != nullptr);
    }

    BarrierBatch::~BarrierBatch()
    {
        Flush();
    }

    void BarrierBatch::Transition(
        GpuResource& resource,
        D3D12_RESOURCE_STATES after,
        uint32_t subresource)
    {
        ID3D12Resource* native = resource.Get();
        if (!native) {
            return;
        }

        auto& states = resource.states_;

        // 個別追跡していない、または全体指定 ―― ただし個別追跡中に状態がばらけている場合は
        // サブリソースごとに 1 本ずつ発行しないと ALL_SUBRESOURCES の StateBefore が定まらない
        const bool wholeResource = (subresource == kAllSubresources);

        if (wholeResource && states.size() > 1 && !resource.HasUniformState()) {
            for (uint32_t i = 0; i < static_cast<uint32_t>(states.size()); ++i) {
                Transition(resource, after, i);
            }
            return;
        }

        const uint32_t index = wholeResource ? 0u : subresource;
        assert(index < states.size());

        const D3D12_RESOURCE_STATES before = states[index];
        if (before == after) {
            return; // 冗長バリアはスキップ
        }

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = native;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        barrier.Transition.Subresource =
            wholeResource ? D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES : subresource;

        barriers_.push_back(barrier);

        // ステートはここで更新する（同一バッチ内の連鎖遷移で before を正しく引き継ぐため）
        if (wholeResource) {
            states.assign(states.size(), after);
        } else {
            states[index] = after;
        }

        LogTransition(native, before, after, barrier.Transition.Subresource);
    }

    void BarrierBatch::UAV(GpuResource& resource)
    {
        if (!resource.Get()) {
            return;
        }

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.UAV.pResource = resource.Get();
        barriers_.push_back(barrier);
    }

    void BarrierBatch::UAVAll()
    {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.UAV.pResource = nullptr;
        barriers_.push_back(barrier);
    }

    void BarrierBatch::Flush()
    {
        if (barriers_.empty()) {
            return;
        }

        cmdList_->ResourceBarrier(static_cast<UINT>(barriers_.size()), barriers_.data());
        barriers_.clear();
    }

    // ---------------------------------------------------------------
    // 単発ショートカット
    // ---------------------------------------------------------------

    namespace Barrier
    {
        void Transition(
            ID3D12GraphicsCommandList* cmdList,
            GpuResource& resource,
            D3D12_RESOURCE_STATES after,
            uint32_t subresource)
        {
            BarrierBatch batch(cmdList);
            batch.Transition(resource, after, subresource);
        }

        void UAV(ID3D12GraphicsCommandList* cmdList, GpuResource& resource)
        {
            BarrierBatch batch(cmdList);
            batch.UAV(resource);
        }

        void UAVAll(ID3D12GraphicsCommandList* cmdList)
        {
            BarrierBatch batch(cmdList);
            batch.UAVAll();
        }

        void UAVRaw(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* resource)
        {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrier.UAV.pResource = resource;
            cmdList->ResourceBarrier(1, &barrier);
        }
    }
}
