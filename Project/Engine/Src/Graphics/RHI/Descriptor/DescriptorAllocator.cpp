#include "pch.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"

#include "Utility/Logger/Logger.h"

#include <cassert>

namespace CoreEngine
{
    void DescriptorAllocator::Initialize(ID3D12Device* device, uint32_t maxSRV, uint32_t maxRTV, uint32_t maxDSV)
    {
        assert(device != nullptr && "Device must not be null");
        device_ = device;

        srvHeap_.Initialize(device, DescriptorHeapType::SRV_CBV_UAV, maxSRV, "SRV/CBV/UAV");
        rtvHeap_.Initialize(device, DescriptorHeapType::RTV, maxRTV, "RTV");
        dsvHeap_.Initialize(device, DescriptorHeapType::DSV, maxDSV, "DSV");

        Logger::GetInstance().Infof(LogCategory::Graphics, LogSubCategory::Heap,
            "DescriptorAllocator初期化完了: SRV={}, RTV={}, DSV={}\n", maxSRV, maxRTV, maxDSV);
    }

    void DescriptorAllocator::Shutdown()
    {
        srvHeap_.Shutdown();
        rtvHeap_.Shutdown();
        dsvHeap_.Shutdown();
        device_ = nullptr;
    }

    // ================================================================
    // 生成
    // ================================================================

    DescriptorHandle DescriptorAllocator::CreateSRV(ID3D12Resource* resource,
        const D3D12_SHADER_RESOURCE_VIEW_DESC& desc, std::string_view debugName)
    {
        // resource == nullptr は TLAS SRV (RAYTRACING_ACCELERATION_STRUCTURE) で正当
        assert((resource != nullptr
            || desc.ViewDimension == D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE)
            && "Resource must not be null (except for Raytracing Acceleration Structure SRV)");

        DescriptorHandle handle = srvHeap_.Allocate(debugName);
        device_->CreateShaderResourceView(resource, &desc, handle.cpuHandle);
        return handle;
    }

    DescriptorHandle DescriptorAllocator::CreateUAV(ID3D12Resource* resource,
        const D3D12_UNORDERED_ACCESS_VIEW_DESC& desc, std::string_view debugName)
    {
        assert(resource != nullptr && "Resource must not be null");

        DescriptorHandle handle = srvHeap_.Allocate(debugName);
        device_->CreateUnorderedAccessView(resource, nullptr, &desc, handle.cpuHandle);
        return handle;
    }

    DescriptorHandle DescriptorAllocator::CreateCBV(
        const D3D12_CONSTANT_BUFFER_VIEW_DESC& desc, std::string_view debugName)
    {
        DescriptorHandle handle = srvHeap_.Allocate(debugName);
        device_->CreateConstantBufferView(&desc, handle.cpuHandle);
        return handle;
    }

    DescriptorHandle DescriptorAllocator::CreateRTV(ID3D12Resource* resource,
        const D3D12_RENDER_TARGET_VIEW_DESC& desc, std::string_view debugName)
    {
        assert(resource != nullptr && "Resource must not be null");

        DescriptorHandle handle = rtvHeap_.Allocate(debugName);
        device_->CreateRenderTargetView(resource, &desc, handle.cpuHandle);
        return handle;
    }

    DescriptorHandle DescriptorAllocator::CreateDSV(ID3D12Resource* resource,
        const D3D12_DEPTH_STENCIL_VIEW_DESC& desc, std::string_view debugName)
    {
        assert(resource != nullptr && "Resource must not be null");

        DescriptorHandle handle = dsvHeap_.Allocate(debugName);
        device_->CreateDepthStencilView(resource, &desc, handle.cpuHandle);
        return handle;
    }

    // ================================================================
    // 既存スロットへの書き直し
    // ================================================================

    void DescriptorAllocator::WriteSRV(const DescriptorHandle& handle, ID3D12Resource* resource,
        const D3D12_SHADER_RESOURCE_VIEW_DESC& desc)
    {
        assert(handle.IsValid() && "未確保のハンドルへ書き込もうとしました");
        if (!handle.IsValid()) { return; }
        device_->CreateShaderResourceView(resource, &desc, handle.cpuHandle);
    }

    void DescriptorAllocator::WriteUAV(const DescriptorHandle& handle, ID3D12Resource* resource,
        const D3D12_UNORDERED_ACCESS_VIEW_DESC& desc)
    {
        assert(handle.IsValid() && "未確保のハンドルへ書き込もうとしました");
        if (!handle.IsValid()) { return; }
        device_->CreateUnorderedAccessView(resource, nullptr, &desc, handle.cpuHandle);
    }

    void DescriptorAllocator::WriteRTV(const DescriptorHandle& handle, ID3D12Resource* resource,
        const D3D12_RENDER_TARGET_VIEW_DESC& desc)
    {
        assert(handle.IsValid() && "未確保のハンドルへ書き込もうとしました");
        if (!handle.IsValid()) { return; }
        device_->CreateRenderTargetView(resource, &desc, handle.cpuHandle);
    }

    void DescriptorAllocator::WriteDSV(const DescriptorHandle& handle, ID3D12Resource* resource,
        const D3D12_DEPTH_STENCIL_VIEW_DESC& desc)
    {
        assert(handle.IsValid() && "未確保のハンドルへ書き込もうとしました");
        if (!handle.IsValid()) { return; }
        device_->CreateDepthStencilView(resource, &desc, handle.cpuHandle);
    }

    // ================================================================
    // 未確保なら確保、確保済みなら書き直し
    // ================================================================

    void DescriptorAllocator::EnsureSRV(DescriptorHandle& handle, ID3D12Resource* resource,
        const D3D12_SHADER_RESOURCE_VIEW_DESC& desc, std::string_view debugName)
    {
        if (handle.IsValid()) {
            WriteSRV(handle, resource, desc);
            return;
        }
        handle = CreateSRV(resource, desc, debugName);
    }

    void DescriptorAllocator::EnsureUAV(DescriptorHandle& handle, ID3D12Resource* resource,
        const D3D12_UNORDERED_ACCESS_VIEW_DESC& desc, std::string_view debugName)
    {
        if (handle.IsValid()) {
            WriteUAV(handle, resource, desc);
            return;
        }
        handle = CreateUAV(resource, desc, debugName);
    }

    // ================================================================
    // 確保のみ / 解放
    // ================================================================

    DescriptorHandle DescriptorAllocator::AllocateSRVHandle(std::string_view debugName)
    {
        return srvHeap_.Allocate(debugName);
    }

    DescriptorHandle DescriptorAllocator::AllocateRTVHandle(std::string_view debugName)
    {
        return rtvHeap_.Allocate(debugName);
    }

    DescriptorHandle DescriptorAllocator::AllocateDSVHandle(std::string_view debugName)
    {
        return dsvHeap_.Allocate(debugName);
    }

    void DescriptorAllocator::Free(DescriptorHandle& handle)
    {
        switch (handle.heapType) {
        case DescriptorHeapType::SRV_CBV_UAV: srvHeap_.Free(handle); break;
        case DescriptorHeapType::RTV:         rtvHeap_.Free(handle); break;
        case DescriptorHeapType::DSV:         dsvHeap_.Free(handle); break;
        default:
            Logger::GetInstance().Warnf(LogCategory::Graphics, LogSubCategory::Heap,
                "Free: 無効なハンドルを解放しようとしました（すでに解放済みか未確保）\n");
            break;
        }
    }
}
