#include "pch.h"
#include "Graphics/RHI/Resource/UavTexture.h"

#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/Resource/GpuResource.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"

#include <algorithm>
#include <exception>

namespace CoreEngine
{
    uint32_t UavTexture::ClampMipLevels(uint32_t smallestExtent, uint32_t desired)
    {
        uint32_t available = 1;
        while ((smallestExtent >> available) >= 1u && available < 16u) {
            ++available;
        }
        return std::max(1u, std::min(desired, available));
    }

    D3D12_RESOURCE_DESC UavTexture::MakeDesc(uint32_t width, uint32_t height, uint32_t depth,
                                             DXGI_FORMAT format, uint32_t mipLevels)
    {
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = (depth > 1) ? D3D12_RESOURCE_DIMENSION_TEXTURE3D
                                     : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = static_cast<UINT16>(std::max(depth, 1u));
        desc.MipLevels = static_cast<UINT16>(std::max(mipLevels, 1u));
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        return desc;
    }

    void UavTexture::MakeViewDescs(const D3D12_RESOURCE_DESC& desc,
                                   D3D12_SHADER_RESOURCE_VIEW_DESC& outSrv,
                                   D3D12_UNORDERED_ACCESS_VIEW_DESC& outUav,
                                   uint32_t mip, uint32_t srvMipCount)
    {
        outSrv = {};
        outSrv.Format = desc.Format;
        outSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        outUav = {};
        outUav.Format = desc.Format;

        if (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D) {
            outSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
            outSrv.Texture3D.MostDetailedMip = mip;
            outSrv.Texture3D.MipLevels = srvMipCount;
            outUav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
            outUav.Texture3D.MipSlice = mip;
            outUav.Texture3D.WSize = std::max<UINT>(desc.DepthOrArraySize >> mip, 1u);
        } else {
            outSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            outSrv.Texture2D.MostDetailedMip = mip;
            outSrv.Texture2D.MipLevels = srvMipCount;
            outUav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            outUav.Texture2D.MipSlice = mip;
        }
    }

    bool UavTexture::CreateResource(const Microsoft::WRL::ComPtr<ID3D12Device>& device,
                                    GpuResource& texture,
                                    const D3D12_RESOURCE_DESC& desc,
                                    D3D12_RESOURCE_STATES initialState,
                                    uint32_t subresourceCount)
    {
        try {
            texture.Reset(ResourceFactory::CreateTextureResource(device, desc, initialState),
                initialState, subresourceCount);
        }
        catch (const std::exception&) {
            return false;
        }
        return true;
    }

    void UavTexture::EnsureViews(DescriptorAllocator& allocator, ID3D12Resource* resource,
                                 const D3D12_RESOURCE_DESC& desc,
                                 DescriptorHandle& srv, std::string_view srvName,
                                 DescriptorHandle& uav, std::string_view uavName)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        MakeViewDescs(desc, srvDesc, uavDesc, 0, desc.MipLevels);
        allocator.EnsureSRV(srv, resource, srvDesc, srvName);
        allocator.EnsureUAV(uav, resource, uavDesc, uavName);
    }

    bool UavTexture::Create(const Microsoft::WRL::ComPtr<ID3D12Device>& device,
                            DescriptorAllocator& allocator,
                            GpuResource& texture,
                            const D3D12_RESOURCE_DESC& desc,
                            DescriptorHandle& srv, std::string_view srvName,
                            DescriptorHandle& uav, std::string_view uavName)
    {
        if (!CreateResource(device, texture, desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, desc.MipLevels)) {
            return false;
        }
        EnsureViews(allocator, texture.Get(), desc, srv, srvName, uav, uavName);
        return true;
    }
}
