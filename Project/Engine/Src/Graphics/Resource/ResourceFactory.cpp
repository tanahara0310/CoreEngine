#include "ResourceFactory.h"
#include <cassert>
#include <format>
#include <stdexcept>
#include <dxgi.h>

#include "Utility/Logger/Logger.h"


namespace CoreEngine
{
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

    D3D12_RESOURCE_DESC resourceDesc {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Width = alignedSize;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

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
}
