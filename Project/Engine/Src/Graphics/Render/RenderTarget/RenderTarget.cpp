#include "pch.h"
#include "RenderTarget.h"
#include "Graphics/Common/ResourceBarrierHelper.h"

namespace CoreEngine
{
    void RenderTarget::TransitionBarrier(
        ID3D12GraphicsCommandList* cmdList,
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES& stateBefore,
        D3D12_RESOURCE_STATES stateAfter)
    {
        ResourceBarrierHelper::Transition(cmdList, resource, stateBefore, stateAfter);
    }
}
