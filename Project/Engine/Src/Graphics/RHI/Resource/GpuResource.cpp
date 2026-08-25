#include "pch.h"
#include "Graphics/RHI/Resource/GpuResource.h"

#include <cassert>

namespace CoreEngine
{
    void GpuResource::Reset(
        Microsoft::WRL::ComPtr<ID3D12Resource> resource,
        D3D12_RESOURCE_STATES initialState,
        uint32_t subresourceCount)
    {
        assert(subresourceCount >= 1);

        resource_ = std::move(resource);
        states_.assign(resource_ ? (std::max)(subresourceCount, 1u) : 1u, initialState);
    }

    void GpuResource::Release()
    {
        resource_.Reset();
        states_.assign(1, D3D12_RESOURCE_STATE_COMMON);
    }

    D3D12_GPU_VIRTUAL_ADDRESS GpuResource::GpuAddress() const
    {
        return resource_ ? resource_->GetGPUVirtualAddress() : D3D12_GPU_VIRTUAL_ADDRESS{ 0 };
    }

    D3D12_RESOURCE_DESC GpuResource::Desc() const
    {
        return resource_ ? resource_->GetDesc() : D3D12_RESOURCE_DESC{};
    }

    D3D12_RESOURCE_STATES GpuResource::State(uint32_t subresource) const
    {
        if (subresource == kAllSubresources || states_.size() == 1) {
            return states_[0];
        }

        assert(subresource < states_.size());
        return states_[subresource];
    }

    bool GpuResource::HasUniformState() const noexcept
    {
        for (size_t i = 1; i < states_.size(); ++i) {
            if (states_[i] != states_[0]) {
                return false;
            }
        }
        return true;
    }

    void GpuResource::DeclareState(D3D12_RESOURCE_STATES state, uint32_t subresource)
    {
        if (subresource == kAllSubresources) {
            states_.assign(states_.size(), state);
            return;
        }

        assert(subresource < states_.size());
        states_[subresource] = state;
    }
}
