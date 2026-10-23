#pragma once

#include <d3d12.h>
#include <wrl.h>


namespace CoreEngine
{
/// @brief D3D12 のバッファ・テクスチャリソースを作る
namespace ResourceFactory {
    // バッファリソースを生成する（定数バッファ・頂点バッファ等はUPLOAD、リードバックはREADBACK）
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferResource(
        const Microsoft::WRL::ComPtr<ID3D12Device>& device,
        size_t sizeInBytes,
        D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_UPLOAD);

    // DEFAULTヒープ上にテクスチャリソースを生成する（RenderTarget・SRV・DSV・COPY_DEST等）
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateTextureResource(
        const Microsoft::WRL::ComPtr<ID3D12Device>& device,
        const D3D12_RESOURCE_DESC& desc,
        D3D12_RESOURCE_STATES initialState,
        const D3D12_CLEAR_VALUE* clearValue = nullptr);

    // 四角形 1 枚（0,1,2 / 1,3,2）のインデックスバッファを生成する（同じ四角形を描くものはこれを共有する）
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateQuadIndexBuffer(
        const Microsoft::WRL::ComPtr<ID3D12Device>& device,
        D3D12_INDEX_BUFFER_VIEW& outView);
}
}
