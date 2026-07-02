#include "pch.h"
#include "RayTracingOutputViewSet.h"

#include "Graphics/Common/Core/DescriptorManager.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    bool RayTracingOutputViewSet::EnsureTexture(
        DirectXCommon* dxCommon,
        DescriptorManager* descriptorManager,
        UINT width,
        UINT height,
        uint32_t viewIndex,
        const char* ownerName,
        const std::string& uavDebugName,
        const std::string& srvDebugName,
        DXGI_FORMAT format)
    {
        View& view = views_[viewIndex];
        if (view.texture && view.width == width && view.height == height) {
            return true;
        }

        view.texture.Reset();
        view.uavHandle = {};
        view.uavCpuHandle = {};
        view.srvHandle = {};
        view.srvCpuHandle = {};
        view.width = width;
        view.height = height;
        view.currentState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC texDesc{};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Width = width;
        texDesc.Height = height;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels = 1;
        texDesc.Format = format;
        texDesc.SampleDesc.Count = 1;
        texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        HRESULT hr = dxCommon->GetDevice()->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &texDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr,
            IID_PPV_ARGS(&view.texture));
        if (FAILED(hr)) {
            Logger::GetInstance().Errorf(
                LogCategory::Graphics,
                LogSubCategory::RenderTarget,
                "{}: output texture allocation failed. viewIndex={} width={} height={} hr={:#x}",
                ownerName,
                viewIndex,
                width,
                height,
                static_cast<uint32_t>(hr));
            return false;
        }

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = format;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        descriptorManager->CreateUAV(
            view.texture.Get(),
            uavDesc,
            view.uavCpuHandle,
            view.uavHandle,
            uavDebugName);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        descriptorManager->CreateSRV(
            view.texture.Get(),
            srvDesc,
            view.srvCpuHandle,
            view.srvHandle,
            srvDebugName);

        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::RenderTarget,
            "{}: output texture ready. viewIndex={} width={} height={} uav=0x{:X} srv=0x{:X}",
            ownerName,
            viewIndex,
            width,
            height,
            view.uavHandle.ptr,
            view.srvHandle.ptr);

        return true;
    }

    void RayTracingOutputViewSet::ReleaseIfSizeMismatch(UINT width, UINT height, uint32_t viewIndex)
    {
        View& view = views_[viewIndex];
        if (view.width == width && view.height == height) {
            return;
        }

        view.texture.Reset();
        view.uavHandle = {};
        view.uavCpuHandle = {};
        view.srvHandle = {};
        view.srvCpuHandle = {};
        view.width = width;
        view.height = height;
        view.currentState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
}
