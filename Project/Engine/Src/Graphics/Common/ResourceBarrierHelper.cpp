#include "pch.h"
#include "ResourceBarrierHelper.h"

#include <cassert>

namespace CoreEngine
{
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
    }
}
