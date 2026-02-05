#include "ResourceFactory.h"
#include <cassert>
#include <stdexcept>


namespace CoreEngine
{
Microsoft::WRL::ComPtr<ID3D12Resource> ResourceFactory::CreateBufferResource(Microsoft::WRL::ComPtr<ID3D12Device> device, size_t sizeInBytes)
{
    // UPLOAD_HEAP用のヒープ設定
    // D3D12_HEAP_TYPE_UPLOAD: CPUからGPUへのアップロード用
    // このタイプのリソースは永続的にマップすることが推奨される（Microsoft公式ガイドライン）
    D3D12_HEAP_PROPERTIES heapProperties {};
    heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
    heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    // バッファリソースの設定
    D3D12_RESOURCE_DESC resourceDesc {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    // 256バイトアライメント（D3D12の要件）
    size_t alignedSize = (sizeInBytes + 255) & ~0xFF;
    resourceDesc.Width = alignedSize;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    // バッファリソースを生成
    Microsoft::WRL::ComPtr<ID3D12Resource> bufferResource;
    HRESULT hr = device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&bufferResource));

    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create BufferResource");
    }

    return bufferResource;
}
}
