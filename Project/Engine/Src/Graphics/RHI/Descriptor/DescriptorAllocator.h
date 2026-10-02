#pragma once

#include <d3d12.h>
#include <cstdint>
#include <string_view>

#include "Graphics/RHI/Descriptor/DescriptorHandle.h"
#include "Graphics/RHI/Descriptor/DescriptorHeapAllocator.h"

namespace CoreEngine
{
    class DeferredReleaseQueue;

    /// @brief SRV/CBV/UAV・RTV・DSV の 3 ヒープを束ねるディスクリプタ確保器
    /// @details すべての生成 API は確保したスロットを `DescriptorHandle` で返す。
    ///
    /// 使い分け:
    /// - `CreateXxx()`  … スロットを確保してビューを書く（新規）
    /// - `WriteXxx()`   … 確保済みスロットへビューだけ書き直す（リソース再作成時。スロット番号は変わらない）
    /// - `EnsureXxx()`  … 未確保なら Create、確保済みなら Write（リサイズ経路の定型）
    ///
    /// 返すスロットは `UniqueDescriptor` に持たせる。手放すと GPU が使い終わってから返る。
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

        /// @brief 手放したスロットを預ける遅延解放キューをつなぐ（nullptr で外す）
        /// @note つないでいない間に手放されたスロットは返さない（終了処理でヒープごと消える）。
        void SetDeferredReleaseQueue(DeferredReleaseQueue* queue) { deferredRelease_ = queue; }

        // ── ヒープ参照 ──────────────────────────────────────────
        ID3D12DescriptorHeap* GetSRVHeap() const { return srvHeap_.Heap(); }
        /// @brief SRV/CBV/UAV ヒープ内のインデックスを GPU ハンドルから求める（ヒープの外なら UINT32_MAX）
        /// @details シェーダーの ResourceDescriptorHeap[] に渡す番号
        uint32_t GetSRVHeapIndex(D3D12_GPU_DESCRIPTOR_HANDLE handle) const { return srvHeap_.IndexOf(handle); }
        ID3D12DescriptorHeap* GetRTVHeap() const { return rtvHeap_.Heap(); }
        ID3D12DescriptorHeap* GetDSVHeap() const { return dsvHeap_.Heap(); }

        // ── 使用状況（デバッグ表示用） ──────────────────────────
        const DescriptorHeapAllocator& SRV() const { return srvHeap_; }
        const DescriptorHeapAllocator& RTV() const { return rtvHeap_; }
        const DescriptorHeapAllocator& DSV() const { return dsvHeap_; }

    private:
        friend class DeferredReleaseQueue;
        friend class UniqueDescriptor;

        /// @brief スロットを今すぐ返す（種別はハンドルが持っているので自動で振り分ける）
        /// @note GPU がそのスロットを読み終えた後に、遅延解放キューから呼ばれる。
        void Free(DescriptorHandle& handle);

        /// @brief 記録中のフレームの GPU 作業が終わってからスロットを返す（UniqueDescriptor が手放すときに呼ぶ）
        void Retire(DescriptorHandle& handle);

        ID3D12Device* device_ = nullptr;

        DescriptorHeapAllocator srvHeap_;
        DescriptorHeapAllocator rtvHeap_;
        DescriptorHeapAllocator dsvHeap_;

        // 手放したスロットを預ける先（GraphicsCore がつなぐ）
        DeferredReleaseQueue* deferredRelease_ = nullptr;
    };
}
