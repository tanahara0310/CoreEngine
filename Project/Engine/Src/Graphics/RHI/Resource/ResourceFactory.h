#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>


namespace CoreEngine
{
/// @brief D3D12 のバッファ・テクスチャリソースを作る
namespace ResourceFactory {
    // バッファリソースを生成する（定数バッファ・頂点バッファ等はUPLOAD、リードバックはREADBACK）
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferResource(
        const Microsoft::WRL::ComPtr<ID3D12Device>& device,
        size_t sizeInBytes,
        D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_UPLOAD);

    /// @brief UPLOAD ヒープにバッファを作り、data から sizeInBytes バイトを先頭へ写す
    /// @details 作った後に中身を書き換えないバッファ用。写し終えたら Unmap する。
    ///          Map に失敗したときはログを残し、写さないままのバッファを返す
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateStaticBufferFromBytes(
        const Microsoft::WRL::ComPtr<ID3D12Device>& device,
        const void* data,
        size_t sizeInBytes);

    /// @brief UPLOAD ヒープにバッファを作り、values を先頭から並べて写す（作った後に書き換えない頂点・インデックスなど）
    /// @param values 写す要素（呼び出し側は CreateStaticBuffer<要素の型>(device, vector など) と型を書く）
    /// @note バッファの大きさは values のバイト数を 256 バイト単位に切り上げたもの
    template <class T>
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateStaticBuffer(
        const Microsoft::WRL::ComPtr<ID3D12Device>& device,
        std::span<const T> values)
    {
        static_assert(std::is_trivially_copyable_v<T>,
            "ResourceFactory::CreateStaticBuffer: 要素は trivially copyable であること");
        return CreateStaticBufferFromBytes(device, values.data(), values.size_bytes());
    }

    /// @brief UPLOAD ヒープにバッファを作り、value を 1 つ写す（作った後に書き換えない定数など）
    template <class T>
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateStaticBuffer(
        const Microsoft::WRL::ComPtr<ID3D12Device>& device,
        const T& value)
    {
        return CreateStaticBuffer<T>(device, std::span<const T>(&value, 1));
    }

    // DEFAULTヒープ上にテクスチャリソースを生成する（RenderTarget・SRV・DSV・COPY_DEST等）
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateTextureResource(
        const Microsoft::WRL::ComPtr<ID3D12Device>& device,
        const D3D12_RESOURCE_DESC& desc,
        D3D12_RESOURCE_STATES initialState,
        const D3D12_CLEAR_VALUE* clearValue = nullptr);

    // 四角形 quadCount 枚（n 枚目は 4n+0,4n+1,4n+2 / 4n+1,4n+3,4n+2）のインデックスバッファを生成する
    // （同じ四角形を描くものはこれを共有する）
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateQuadIndexBuffer(
        const Microsoft::WRL::ComPtr<ID3D12Device>& device,
        D3D12_INDEX_BUFFER_VIEW& outView,
        uint32_t quadCount = 1);
}
}
