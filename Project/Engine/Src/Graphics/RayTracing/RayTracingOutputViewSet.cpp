#include "pch.h"
#include "RayTracingOutputViewSet.h"

#include <cassert>

#include "Graphics/RHI/Command/DeferredReleaseQueue.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    bool RayTracingOutputViewSet::EnsureTexture(
        GraphicsCore* dxCommon,
        DescriptorAllocator* descriptorAllocator,
        UINT width,
        UINT height,
        uint32_t slotIndex,
        const char* ownerName,
        const std::string& debugName,
        const TextureOptions& options)
    {
        assert(slotIndex < kMaxSlotCount && "RayTracingOutputViewSet: slot index out of range");

        Slot& slot = slots_[slotIndex];
        if (slot.texture && slot.width == width && slot.height == height && slot.format == options.format) {
            return true;
        }

        // 前のテクスチャとビューは、記録中のフレームまでの GPU 作業が終わってから手放す
        RetireSlot(dxCommon, descriptorAllocator, slot);
        slot.width = width;
        slot.height = height;
        slot.format = options.format;

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC texDesc{};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Width = width;
        texDesc.Height = height;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels = 1;
        texDesc.Format = options.format;
        texDesc.SampleDesc.Count = 1;
        texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        texDesc.Flags = options.allowUAV
            ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
            : D3D12_RESOURCE_FLAG_NONE;

        Microsoft::WRL::ComPtr<ID3D12Resource> texture;
        HRESULT hr = dxCommon->GetDevice()->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &texDesc,
            options.initialState,
            nullptr,
            IID_PPV_ARGS(&texture));
        if (FAILED(hr)) {
            Logger::GetInstance().Errorf(
                LogCategory::Graphics,
                LogSubCategory::RenderTarget,
                "{}: output texture allocation failed. slot={} width={} height={} hr={:#x}",
                ownerName,
                slotIndex,
                width,
                height,
                static_cast<uint32_t>(hr));
            return false;
        }

        slot.texture.Reset(std::move(texture), options.initialState);

        if (options.allowUAV) {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
            uavDesc.Format = options.format;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            descriptorAllocator->EnsureUAV(slot.uavHandle, slot.texture.Get(), uavDesc, debugName + "_UAV");
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = options.format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        descriptorAllocator->EnsureSRV(slot.srvHandle, slot.texture.Get(), srvDesc, debugName + "_SRV");

        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::RenderTarget,
            "{}: output texture ready. slot={} width={} height={} uav=0x{:X} srv=0x{:X}",
            ownerName,
            slotIndex,
            width,
            height,
            slot.uavHandle.gpuHandle.ptr,
            slot.srvHandle.gpuHandle.ptr);

        return true;
    }

    void RayTracingOutputViewSet::ReleaseIfSizeMismatch(
        GraphicsCore* dxCommon,
        DescriptorAllocator* descriptorAllocator,
        UINT width,
        UINT height,
        uint32_t slotIndex)
    {
        assert(slotIndex < kMaxSlotCount && "RayTracingOutputViewSet: slot index out of range");

        Slot& slot = slots_[slotIndex];
        if (slot.width == width && slot.height == height) {
            return;
        }

        RetireSlot(dxCommon, descriptorAllocator, slot);
        slot.width = width;
        slot.height = height;
    }

    void RayTracingOutputViewSet::RetireSlot(
        GraphicsCore* dxCommon, DescriptorAllocator* descriptorAllocator, Slot& slot)
    {
        if (!slot.texture && !slot.uavHandle.IsValid() && !slot.srvHandle.IsValid()) {
            return;
        }
        assert(dxCommon && descriptorAllocator && "RayTracingOutputViewSet: slot owns resources without a device");

        DeferredReleaseQueue& deferredRelease = dxCommon->DeferredRelease();
        if (slot.texture) {
            deferredRelease.PushForCurrentFrame(Microsoft::WRL::ComPtr<ID3D12Resource>(slot.texture.Get()));
            slot.texture.Release();
        }
        deferredRelease.PushForCurrentFrame(*descriptorAllocator, slot.uavHandle);
        deferredRelease.PushForCurrentFrame(*descriptorAllocator, slot.srvHandle);
    }
}
