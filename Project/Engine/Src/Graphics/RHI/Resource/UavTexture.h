#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include <string_view>

namespace CoreEngine
{
    class DescriptorAllocator;
    class GpuResource;
    struct DescriptorHandle;

    /// @brief コンピュートシェーダが書く UAV 付きテクスチャ（配列でない 2D/3D）を作る
    namespace UavTexture {
        /// @brief 一辺のサイズから作れるミップ段数を求め、上限で切る
        uint32_t ClampMipLevels(uint32_t smallestExtent, uint32_t desired);

        /// @brief UAV 付きテクスチャの Desc を作る（depth が 2 以上なら 3D、1 なら 2D）
        D3D12_RESOURCE_DESC MakeDesc(uint32_t width, uint32_t height, uint32_t depth,
                                     DXGI_FORMAT format, uint32_t mipLevels = 1);

        /// @brief 指定ミップ段を指す SRV/UAV の Desc を Desc の次元から組み立てる
        /// @param mip 対象のミップ段
        /// @param srvMipCount SRV が見る段数（サンプル用の全段ビューは全体を渡す）
        void MakeViewDescs(const D3D12_RESOURCE_DESC& desc,
                           D3D12_SHADER_RESOURCE_VIEW_DESC& outSrv,
                           D3D12_UNORDERED_ACCESS_VIEW_DESC& outUav,
                           uint32_t mip = 0, uint32_t srvMipCount = 1);

        /// @brief DEFAULT ヒープにリソースを作り、texture へ持たせる
        /// @param subresourceCount 個別に追跡するサブリソース数（GpuResource::Reset と同じ）
        /// @return 作れなければ false（texture はそのまま）
        bool CreateResource(const Microsoft::WRL::ComPtr<ID3D12Device>& device,
                            GpuResource& texture,
                            const D3D12_RESOURCE_DESC& desc,
                            D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                            uint32_t subresourceCount = 1);

        /// @brief 全段を見る SRV と段 0 へ書く UAV を、この順に書く（既存スロットがあればそこへ書き直す）
        void EnsureViews(DescriptorAllocator& allocator, ID3D12Resource* resource,
                         const D3D12_RESOURCE_DESC& desc,
                         DescriptorHandle& srv, std::string_view srvName,
                         DescriptorHandle& uav, std::string_view uavName);

        /// @brief UNORDERED_ACCESS のテクスチャをミップの段ごとに追跡して作り、EnsureViews で SRV/UAV を書く
        /// @return テクスチャを作れなければ false（ビューは書かない）
        bool Create(const Microsoft::WRL::ComPtr<ID3D12Device>& device,
                    DescriptorAllocator& allocator,
                    GpuResource& texture,
                    const D3D12_RESOURCE_DESC& desc,
                    DescriptorHandle& srv, std::string_view srvName,
                    DescriptorHandle& uav, std::string_view uavName);
    }
}
