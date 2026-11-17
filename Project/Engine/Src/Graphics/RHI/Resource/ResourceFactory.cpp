#include "pch.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include <cassert>
#include <cstring>
#include <format>
#include <stdexcept>
#include <vector>
#include <dxgi.h>

#include "Utility/Logger/Logger.h"


namespace CoreEngine
{
D3D12_RESOURCE_DESC ResourceFactory::MakeBufferDesc(UINT64 sizeInBytes, D3D12_RESOURCE_FLAGS flags)
{
    D3D12_RESOURCE_DESC desc {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = sizeInBytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = flags;
    return desc;
}

Microsoft::WRL::ComPtr<ID3D12Resource> ResourceFactory::CreateBufferResource(
    const Microsoft::WRL::ComPtr<ID3D12Device>& device,
    size_t sizeInBytes,
    D3D12_HEAP_TYPE heapType)
{
    D3D12_HEAP_PROPERTIES heapProperties {};
    heapProperties.Type = heapType;
    heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    // 256バイトアライメント（D3D12の要件）
    const size_t alignedSize = (sizeInBytes + 255) & ~static_cast<size_t>(0xFF);
    const D3D12_RESOURCE_DESC resourceDesc = MakeBufferDesc(alignedSize);

    // READBACKヒープはCOPY_DEST、それ以外はGENERIC_READ
    const D3D12_RESOURCE_STATES initialState = (heapType == D3D12_HEAP_TYPE_READBACK)
        ? D3D12_RESOURCE_STATE_COPY_DEST
        : D3D12_RESOURCE_STATE_GENERIC_READ;

    Microsoft::WRL::ComPtr<ID3D12Resource> bufferResource;
    HRESULT hr = device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        initialState,
        nullptr,
        IID_PPV_ARGS(&bufferResource));

    if (FAILED(hr)) {
        std::string errorMsg = std::format(
            "Failed to create BufferResource (size={}, HRESULT=0x{:08X})",
            sizeInBytes, static_cast<unsigned int>(hr));

        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            HRESULT removedReason = device->GetDeviceRemovedReason();
            errorMsg += std::format(" DeviceRemovedReason=0x{:08X}", static_cast<unsigned int>(removedReason));
        }

        Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics, "{}", errorMsg);
        throw std::runtime_error(errorMsg);
    }

    return bufferResource;
}

Microsoft::WRL::ComPtr<ID3D12Resource> ResourceFactory::CreateStaticBufferFromBytes(
    const Microsoft::WRL::ComPtr<ID3D12Device>& device,
    const void* data,
    size_t sizeInBytes)
{
    Microsoft::WRL::ComPtr<ID3D12Resource> bufferResource = CreateBufferResource(device, sizeInBytes);

    void* mapped = nullptr;
    if (FAILED(bufferResource->Map(0, nullptr, &mapped)) || !mapped) {
        Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics,
            "CreateStaticBuffer: バッファの Map に失敗しました（size={}）", sizeInBytes);
        return bufferResource;
    }
    if (data && sizeInBytes > 0) {
        std::memcpy(mapped, data, sizeInBytes);
    }
    bufferResource->Unmap(0, nullptr);
    return bufferResource;
}

Microsoft::WRL::ComPtr<ID3D12Resource> ResourceFactory::CreateTextureResource(
    const Microsoft::WRL::ComPtr<ID3D12Device>& device,
    const D3D12_RESOURCE_DESC& desc,
    D3D12_RESOURCE_STATES initialState,
    const D3D12_CLEAR_VALUE* clearValue)
{
    D3D12_HEAP_PROPERTIES heapProperties {};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    Microsoft::WRL::ComPtr<ID3D12Resource> textureResource;
    HRESULT hr = device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        initialState,
        clearValue,
        IID_PPV_ARGS(&textureResource));

    if (FAILED(hr)) {
        std::string errorMsg = std::format(
            "Failed to create TextureResource ({}x{}, format={}, HRESULT=0x{:08X})",
            desc.Width, desc.Height,
            static_cast<unsigned int>(desc.Format),
            static_cast<unsigned int>(hr));

        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            HRESULT removedReason = device->GetDeviceRemovedReason();
            errorMsg += std::format(" DeviceRemovedReason=0x{:08X}", static_cast<unsigned int>(removedReason));
        }

        Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics, "{}", errorMsg);
        throw std::runtime_error(errorMsg);
    }

    return textureResource;
}

Microsoft::WRL::ComPtr<ID3D12Resource> ResourceFactory::CreateQuadIndexBuffer(
    const Microsoft::WRL::ComPtr<ID3D12Device>& device,
    D3D12_INDEX_BUFFER_VIEW& outView,
    uint32_t quadCount)
{
    assert(quadCount > 0 && "CreateQuadIndexBuffer: quadCount は 1 以上");

    constexpr uint32_t kQuadIndices[6] = { 0, 1, 2, 1, 3, 2 };
    std::vector<uint32_t> indices(static_cast<size_t>(quadCount) * std::size(kQuadIndices));
    for (uint32_t quad = 0; quad < quadCount; ++quad) {
        const uint32_t baseVertex = quad * 4;
        uint32_t* destination = indices.data() + static_cast<size_t>(quad) * std::size(kQuadIndices);
        for (size_t i = 0; i < std::size(kQuadIndices); ++i) {
            destination[i] = baseVertex + kQuadIndices[i];
        }
    }

    const std::span<const uint32_t> indexSpan = indices;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexResource = CreateStaticBuffer<uint32_t>(device, indexSpan);

    outView.BufferLocation = indexResource->GetGPUVirtualAddress();
    outView.SizeInBytes = static_cast<UINT>(indexSpan.size_bytes());
    outView.Format = DXGI_FORMAT_R32_UINT;
    return indexResource;
}
}
