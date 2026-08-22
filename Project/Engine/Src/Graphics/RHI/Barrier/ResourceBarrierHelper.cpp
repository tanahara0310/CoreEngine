#include "pch.h"
#include "Graphics/RHI/Barrier/ResourceBarrierHelper.h"
#include "Utility/Logger/Logger.h"

#include <cassert>

namespace CoreEngine
{

#ifdef _DEBUG
    namespace {
        /// @brief D3D12_RESOURCE_STATES を人間が読める文字列に変換する（デバッグログ用）
        std::string ResourceStateToString(D3D12_RESOURCE_STATES state)
        {
            if (state == D3D12_RESOURCE_STATE_COMMON)                         return "COMMON";
            if (state == D3D12_RESOURCE_STATE_RENDER_TARGET)                  return "RENDER_TARGET";
            if (state == D3D12_RESOURCE_STATE_UNORDERED_ACCESS)               return "UNORDERED_ACCESS";
            if (state == D3D12_RESOURCE_STATE_DEPTH_WRITE)                    return "DEPTH_WRITE";
            if (state == D3D12_RESOURCE_STATE_DEPTH_READ)                     return "DEPTH_READ";
            if (state == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)          return "PIXEL_SHADER_RESOURCE";
            if (state == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)      return "NON_PIXEL_SHADER_RESOURCE";
            if (state == D3D12_RESOURCE_STATE_COPY_SOURCE)                    return "COPY_SOURCE";
            if (state == D3D12_RESOURCE_STATE_COPY_DEST)                      return "COPY_DEST";
            if (state == D3D12_RESOURCE_STATE_GENERIC_READ)                   return "GENERIC_READ";
            if (state == D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE) return "RAYTRACING_ACCELERATION_STRUCTURE";
            if (state == (D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE))
                return "DEPTH_READ|PIXEL_SHADER_RESOURCE";
            return "UNKNOWN(0x" + std::to_string(static_cast<UINT>(state)) + ")";
        }
    } // anonymous namespace
#endif

    void ResourceBarrierHelper::Transition(
        ID3D12GraphicsCommandList* cmdList,
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES& currentState,
        D3D12_RESOURCE_STATES targetState)
    {
        assert(cmdList);
        assert(resource);

        // 既に目標ステートにある場合はスキップ（冗長バリア防止）
        if (currentState == targetState) return;

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = resource;
        barrier.Transition.StateBefore = currentState;
        barrier.Transition.StateAfter = targetState;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        cmdList->ResourceBarrier(1, &barrier);

#ifdef _DEBUG
        Logger::GetInstance().Logf(LogLevel::Debug, LogCategory::Graphics, LogSubCategory::Barrier,
            "[Barrier] Transition: resource=0x{:X}  {} -> {}",
            reinterpret_cast<uintptr_t>(resource),
            ResourceStateToString(currentState),
            ResourceStateToString(targetState));
#endif

        // バリア発行後にステートを更新
        currentState = targetState;
    }

    void ResourceBarrierHelper::UAV(
        ID3D12GraphicsCommandList* cmdList,
        ID3D12Resource* resource)
    {
        assert(cmdList);

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.UAV.pResource = resource;

        cmdList->ResourceBarrier(1, &barrier);

#ifdef _DEBUG
        Logger::GetInstance().Logf(LogLevel::Debug, LogCategory::Graphics, LogSubCategory::Barrier,
            "[Barrier] UAV: resource=0x{:X}",
            reinterpret_cast<uintptr_t>(resource));
#endif
    }

    // ---------------------------------------------------------------
    // ResourceBarrierBatch
    // ---------------------------------------------------------------

    ResourceBarrierBatch::ResourceBarrierBatch(ID3D12GraphicsCommandList* cmdList)
        : cmdList_(cmdList)
    {
        assert(cmdList_ != nullptr);
    }

    ResourceBarrierBatch::~ResourceBarrierBatch()
    {
        // 未発行のバリアを自動で Flush する
        Flush();
    }

    void ResourceBarrierBatch::Add(
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES& currentState,
        D3D12_RESOURCE_STATES targetState)
    {
        assert(resource != nullptr);

        // 冗長バリアはスキップ
        if (currentState == targetState) return;

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = resource;
        barrier.Transition.StateBefore = currentState;
        barrier.Transition.StateAfter = targetState;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        pendingTransitions_.push_back({ barrier, &currentState });
    }

    void ResourceBarrierBatch::AddUAV(ID3D12Resource* resource)
    {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.UAV.pResource = resource;

        pendingUAVs_.push_back(barrier);
    }

    void ResourceBarrierBatch::Flush()
    {
        // バッチを結合して一括発行
        std::vector<D3D12_RESOURCE_BARRIER> barriers;
        barriers.reserve(pendingTransitions_.size() + pendingUAVs_.size());

        for (const auto& pt : pendingTransitions_) {
            barriers.push_back(pt.barrier);
        }
        for (const auto& uav : pendingUAVs_) {
            barriers.push_back(uav);
        }

        if (!barriers.empty()) {
            cmdList_->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());

#ifdef _DEBUG
            auto& logger = Logger::GetInstance();
            logger.Logf(LogLevel::Debug, LogCategory::Graphics, LogSubCategory::Barrier,
                "[BarrierBatch] Flush: {} barrier(s) 発行",
                barriers.size());
            for (const auto& pt : pendingTransitions_) {
                logger.Logf(LogLevel::Debug, LogCategory::Graphics, LogSubCategory::Barrier,
                    "  Transition: resource=0x{:X}  {} -> {}",
                    reinterpret_cast<uintptr_t>(pt.barrier.Transition.pResource),
                    ResourceStateToString(pt.barrier.Transition.StateBefore),
                    ResourceStateToString(pt.barrier.Transition.StateAfter));
            }
            for (const auto& uav : pendingUAVs_) {
                logger.Logf(LogLevel::Debug, LogCategory::Graphics, LogSubCategory::Barrier,
                    "  UAV: resource=0x{:X}",
                    reinterpret_cast<uintptr_t>(uav.UAV.pResource));
            }
#endif
        }

        // ステート変数を一括で更新（Flush 後に確定）
        for (const auto& pt : pendingTransitions_) {
            *pt.stateRef = pt.barrier.Transition.StateAfter;
        }

        pendingTransitions_.clear();
        pendingUAVs_.clear();
    }

    // ---------------------------------------------------------------
    // ScopedResourceBarrier
    // ---------------------------------------------------------------

    ScopedResourceBarrier::ScopedResourceBarrier(
        ID3D12GraphicsCommandList* cmdList,
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES& currentState,
        D3D12_RESOURCE_STATES targetState)
        : cmdList_(cmdList)
        , resource_(resource)
        , currentState_(currentState)
        , originalState_(currentState)
    {
        // 構築時に即時遷移
        ResourceBarrierHelper::Transition(cmdList_, resource_, currentState_, targetState);
    }

    ScopedResourceBarrier::~ScopedResourceBarrier()
    {
        // スコープ退出時に元のステートへ自動復元
        ResourceBarrierHelper::Transition(cmdList_, resource_, currentState_, originalState_);
    }

} // namespace CoreEngine
