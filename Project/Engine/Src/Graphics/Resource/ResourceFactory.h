#pragma once

#include <d3d12.h>
#include <wrl.h>


namespace CoreEngine
{
class ResourceFactory {
public:
    // バッファリソースを生成する（定数バッファ・頂点バッファ等はUPLOAD、リードバックはREADBACK）
    static Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferResource(
        const Microsoft::WRL::ComPtr<ID3D12Device>& device,
        size_t sizeInBytes,
        D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_UPLOAD);

    // DEFAULTヒープ上にテクスチャリソースを生成する（RenderTarget・SRV・DSV・COPY_DEST等）
    static Microsoft::WRL::ComPtr<ID3D12Resource> CreateTextureResource(
        const Microsoft::WRL::ComPtr<ID3D12Device>& device,
        const D3D12_RESOURCE_DESC& desc,
        D3D12_RESOURCE_STATES initialState,
        const D3D12_CLEAR_VALUE* clearValue = nullptr);
};
}
