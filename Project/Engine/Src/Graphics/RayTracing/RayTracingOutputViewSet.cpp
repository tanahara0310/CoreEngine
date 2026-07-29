#include "pch.h"
#include "RayTracingOutputViewSet.h"

#include <cassert>

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

        // ディスクリプタハンドルは保持したまま再利用する（リサイズ毎の確保はスロットリークになる）
        slot.texture.Reset();
        slot.width = width;
        slot.height = height;
        slot.format = options.format;
        slot.currentState = options.initialState;

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

        HRESULT hr = dxCommon->GetDevice()->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &texDesc,
            options.initialState,
            nullptr,
            IID_PPV_ARGS(&slot.texture));
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

        if (options.allowUAV) {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
            uavDesc.Format = options.format;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            descriptorManager->CreateOrUpdateUAV(
                slot.texture.Get(),
                uavDesc,
                slot.uavCpuHandle,
                slot.uavHandle,
                debugName + "_UAV");
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = options.format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        descriptorManager->CreateOrUpdateSRV(
            slot.texture.Get(),
            srvDesc,
            slot.srvCpuHandle,
            slot.srvHandle,
            debugName + "_SRV");

        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::RenderTarget,
            "{}: output texture ready. slot={} width={} height={} uav=0x{:X} srv=0x{:X}",
            ownerName,
            slotIndex,
            width,
            height,
            slot.uavHandle.ptr,
            slot.srvHandle.ptr);

        return true;
    }

    void RayTracingOutputViewSet::ReleaseIfSizeMismatch(UINT width, UINT height, uint32_t slotIndex)
    {
        assert(slotIndex < kMaxSlotCount && "RayTracingOutputViewSet: slot index out of range");

        Slot& slot = slots_[slotIndex];
        if (slot.width == width && slot.height == height) {
            return;
        }

        // テクスチャのみ解放し、ディスクリプタハンドルは次回 EnsureTexture() で再利用する
        slot.texture.Reset();
        slot.width = width;
        slot.height = height;
    }
}
