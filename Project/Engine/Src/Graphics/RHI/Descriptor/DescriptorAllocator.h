#pragma once

#include <d3d12.h>
#include <cstdint>
#include <string_view>

#include "Graphics/RHI/Descriptor/DescriptorHandle.h"
#include "Graphics/RHI/Descriptor/DescriptorHeapAllocator.h"

namespace CoreEngine
{
    /// @brief SRV/CBV/UAV・RTV・DSV の 3 ヒープを束ねるディスクリプタ確保器
    ///
    /// @details
    /// **すべての生成 API は `DescriptorHandle` を返す。** これが所有権の表明になる。
    /// 旧 API は CPU/GPU ハンドルを出力引数で返すだけでスロット番号を渡さなかったため、
    /// 呼び出し側は確保したスロットを **解放できなかった**（＝作れば作るだけ減る一方だった）。
    ///
    /// 使い分け:
    /// - `CreateXxx()`  … スロットを確保してビューを書く（新規）
    /// - `WriteXxx()`   … 確保済みスロットへビューだけ書き直す（リソース再作成時。スロット番号は変わらない）
    /// - `EnsureXxx()`  … 未確保なら Create、確保済みなら Write（リサイズ経路の定型）
    /// - `Free()`       … スロットを返す。**GPU がそのスロットを読み終えた後**に呼ぶこと
    class DescriptorAllocator
    {
    public:
        // ディスクリプタヒープの既定サイズ
        static constexpr uint32_t kDefaultMaxRTVDescriptors = 256;
        static constexpr uint32_t kDefaultMaxSRVDescriptors = 65536;
        static constexpr uint32_t kDefaultMaxDSVDescriptors = 10;

        void Initialize(ID3D12Device* device,
                        uint32_t maxSRV = kDefaultMaxSRVDescriptors,
                        uint32_t maxRTV = kDefaultMaxRTVDescriptors,
                        uint32_t maxDSV = kDefaultMaxDSVDescriptors);

        void Shutdown();

        // ── 生成（確保 + ビュー書き込み） ───────────────────────

        /// @param resource RAYTRACING_ACCELERATION_STRUCTURE の SRV だけは nullptr が正当
        DescriptorHandle CreateSRV(ID3D12Resource* resource,
                                   const D3D12_SHADER_RESOURCE_VIEW_DESC& desc,
                                   std::string_view debugName = "Unknown");

        DescriptorHandle CreateUAV(ID3D12Resource* resource,
                                   const D3D12_UNORDERED_ACCESS_VIEW_DESC& desc,
                                   std::string_view debugName = "Unknown");

        DescriptorHandle CreateCBV(const D3D12_CONSTANT_BUFFER_VIEW_DESC& desc,
                                   std::string_view debugName = "Unknown");

        DescriptorHandle CreateRTV(ID3D12Resource* resource,
                                   const D3D12_RENDER_TARGET_VIEW_DESC& desc,
                                   std::string_view debugName = "Unknown");

        DescriptorHandle CreateDSV(ID3D12Resource* resource,
                                   const D3D12_DEPTH_STENCIL_VIEW_DESC& desc,
                                   std::string_view debugName = "Unknown");

        // ── 既存スロットへの書き直し ────────────────────────────
        // リソースを作り直したが、シェーダ側のバインド（＝スロット番号）は保ちたいときに使う。

        void WriteSRV(const DescriptorHandle& handle, ID3D12Resource* resource,
                      const D3D12_SHADER_RESOURCE_VIEW_DESC& desc);
        void WriteUAV(const DescriptorHandle& handle, ID3D12Resource* resource,
                      const D3D12_UNORDERED_ACCESS_VIEW_DESC& desc);
        void WriteRTV(const DescriptorHandle& handle, ID3D12Resource* resource,
                      const D3D12_RENDER_TARGET_VIEW_DESC& desc);
        void WriteDSV(const DescriptorHandle& handle, ID3D12Resource* resource,
                      const D3D12_DEPTH_STENCIL_VIEW_DESC& desc);

        // ── 未確保なら確保、確保済みなら書き直し ────────────────
        // リサイズ経路の定型。旧 CreateOrUpdateSRV / CreateOrUpdateUAV の後継。

        void EnsureSRV(DescriptorHandle& handle, ID3D12Resource* resource,
                       const D3D12_SHADER_RESOURCE_VIEW_DESC& desc,
                       std::string_view debugName = "Unknown");
        void EnsureUAV(DescriptorHandle& handle, ID3D12Resource* resource,
                       const D3D12_UNORDERED_ACCESS_VIEW_DESC& desc,
                       std::string_view debugName = "Unknown");

        // ── 確保のみ（ビューは呼び出し側が後で書く） ────────────

        DescriptorHandle AllocateSRVHandle(std::string_view debugName = "Unknown");
        DescriptorHandle AllocateRTVHandle(std::string_view debugName = "Unknown");
        DescriptorHandle AllocateDSVHandle(std::string_view debugName = "Unknown");

        /// @brief スロットを解放する（種別はハンドルが持っているので自動で振り分ける）
        /// @note GPU がそのスロットを参照し終えた後（フェンス完了後）に呼ぶこと
        void Free(DescriptorHandle& handle);

        // ── ヒープ参照 ──────────────────────────────────────────
        ID3D12DescriptorHeap* GetSRVHeap() const { return srvHeap_.Heap(); }
        ID3D12DescriptorHeap* GetRTVHeap() const { return rtvHeap_.Heap(); }
        ID3D12DescriptorHeap* GetDSVHeap() const { return dsvHeap_.Heap(); }

        // ── 使用状況（デバッグ表示用） ──────────────────────────
        const DescriptorHeapAllocator& SRV() const { return srvHeap_; }
        const DescriptorHeapAllocator& RTV() const { return rtvHeap_; }
        const DescriptorHeapAllocator& DSV() const { return dsvHeap_; }

    private:
        ID3D12Device* device_ = nullptr;

        DescriptorHeapAllocator srvHeap_;
        DescriptorHeapAllocator rtvHeap_;
        DescriptorHeapAllocator dsvHeap_;
    };
}
