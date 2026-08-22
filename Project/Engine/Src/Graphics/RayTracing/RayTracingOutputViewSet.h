#pragma once

#include <d3d12.h>
#include "Graphics/RHI/Descriptor/DescriptorHandle.h"
#include <wrl.h>
#include <cstdint>
#include <string>

namespace CoreEngine
{
    class GraphicsCore;
    class DescriptorAllocator;

    /// @brief DXR パスが使う UAV/SRV 出力テクスチャをスロット単位でまとめて管理するクラス
    /// @details EnsureTexture() はサイズ・フォーマット変更時のみ再確保し、UAV/SRV を作り直す。
    ///          各スロットの現在ステートも保持する。
    /// @note スロットの意味は呼び出し元が決める（平坦な添字なので view 単位でも view×ライト×用途でもよい）。
    class RayTracingOutputViewSet {
    public:
        /// @brief 管理可能なスロット数の上限
        /// @details RT シャドウが view(2) × ライト(4) × 用途(6) = 48 スロットを使うため 64 を確保する
        ///          （Stage 3 で履歴 ping-pong 用の 2 枚とハーフ解像度用の中間 2 枚が増えた）。
        static constexpr uint32_t kMaxSlotCount = 64;

        /// @brief テクスチャ 1 枚の作成オプション
        struct TextureOptions {
            DXGI_FORMAT format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            /// @brief UAV を作るか。false ならリソースに ALLOW_UNORDERED_ACCESS も付けない
            ///        （テンポラル履歴のように「コピー先＋SRV」だけで足りるテクスチャ用）
            bool allowUAV = true;
            /// @brief 作成直後のリソースステート
            D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        };

        /// @brief 指定スロットのテクスチャを確保する（サイズ・フォーマットが同じなら何もしない）
        /// @param slotIndex   スロット番号（0 以上 kMaxSlotCount 未満）
        /// @param ownerName   ログ出力用の呼び出し元名
        /// @param debugName   ディスクリプタのデバッグ名の接頭辞（"_UAV" / "_SRV" が付く）
        /// @return 確保に成功した場合 true
        bool EnsureTexture(
            GraphicsCore* dxCommon,
            DescriptorAllocator* descriptorAllocator,
            UINT width,
            UINT height,
            uint32_t slotIndex,
            const char* ownerName,
            const std::string& debugName,
            const TextureOptions& options);

        D3D12_GPU_DESCRIPTOR_HANDLE GetSRVHandle(uint32_t slotIndex) const { return slots_[slotIndex].srvHandle.gpuHandle; }
        D3D12_GPU_DESCRIPTOR_HANDLE GetUAVHandle(uint32_t slotIndex) const { return slots_[slotIndex].uavHandle.gpuHandle; }
        ID3D12Resource* GetResource(uint32_t slotIndex) const { return slots_[slotIndex].texture.Get(); }
        D3D12_RESOURCE_STATES& GetCurrentState(uint32_t slotIndex) { return slots_[slotIndex].currentState; }
        bool HasTexture(uint32_t slotIndex) const { return slots_[slotIndex].texture != nullptr; }
        UINT GetWidth(uint32_t slotIndex) const { return slots_[slotIndex].width; }
        UINT GetHeight(uint32_t slotIndex) const { return slots_[slotIndex].height; }

        /// @brief サイズが一致しない場合のみ、指定スロットのテクスチャを解放する
        /// @details 実際の再生成は次回の EnsureTexture() 呼び出しまで遅延される。
        void ReleaseIfSizeMismatch(UINT width, UINT height, uint32_t slotIndex);

    private:
        /// @brief 出力テクスチャ 1 枚分（実体・UAV/SRV・寸法・現在ステート）
        struct Slot {
            Microsoft::WRL::ComPtr<ID3D12Resource> texture;
            DescriptorHandle uavHandle{};
            DescriptorHandle srvHandle{};
            UINT width = 0;
            UINT height = 0;
            DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
            D3D12_RESOURCE_STATES currentState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        };

        Slot slots_[kMaxSlotCount]{};
    };
}
