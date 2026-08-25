#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <vector>

namespace CoreEngine
{
    class BarrierBatch;

    /// @brief 「全サブリソース」を表す添字
    inline constexpr uint32_t kAllSubresources = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    /// @brief ID3D12Resource と「現在のリソースステート」を 1 つにまとめた所有型
    /// @details 遷移は BarrierBatch / Barrier::Transition を通してのみ行う
    /// @note Reset() の subresourceCount に 2 以上を渡すとサブリソース単位の個別追跡になる
    class GpuResource {
    public:
        GpuResource() = default;
        ~GpuResource() = default;

        // ステートが二重管理になるためコピー禁止。移動のみ許可する
        GpuResource(const GpuResource&) = delete;
        GpuResource& operator=(const GpuResource&) = delete;
        GpuResource(GpuResource&&) noexcept = default;
        GpuResource& operator=(GpuResource&&) noexcept = default;

        /// @brief リソースを差し替え、初期ステートを宣言する
        /// @param resource         保持するリソース（nullptr なら Release と同義）
        /// @param initialState     生成直後のステート（CreateCommittedResource に渡した値）
        /// @param subresourceCount 個別に追跡するサブリソース数。1 なら常に全体を一括で遷移する
        /// @note ComPtr だけ差し替えると前のリソースのステートを引き継ぐので必ずこれを通すこと
        void Reset(Microsoft::WRL::ComPtr<ID3D12Resource> resource,
                   D3D12_RESOURCE_STATES initialState,
                   uint32_t subresourceCount = 1);

        /// @brief リソースを手放す
        void Release();

        ID3D12Resource* Get() const noexcept { return resource_.Get(); }
        bool IsValid() const noexcept { return resource_ != nullptr; }
        explicit operator bool() const noexcept { return IsValid(); }

        /// @brief GPU 仮想アドレス（バッファ用）
        D3D12_GPU_VIRTUAL_ADDRESS GpuAddress() const;

        /// @brief リソース記述子。未保持なら既定構築された値
        D3D12_RESOURCE_DESC Desc() const;

        /// @brief 現在のステート
        /// @param subresource kAllSubresources なら代表値（全体が同一でないときは先頭）を返す
        D3D12_RESOURCE_STATES State(uint32_t subresource = kAllSubresources) const;

        /// @brief 全サブリソースが同じステートか
        bool HasUniformState() const noexcept;

        /// @brief 個別追跡しているサブリソース数（1 なら全体を一括扱い）
        uint32_t SubresourceCount() const noexcept { return static_cast<uint32_t>(states_.size()); }

        /// @brief バリアを介さずステートを宣言し直す
        /// @details 他所が遷移させた事実を追跡へ反映するための最終手段。通常の遷移には使わないこと
        void DeclareState(D3D12_RESOURCE_STATES state, uint32_t subresource = kAllSubresources);

    private:
        friend class BarrierBatch;

        Microsoft::WRL::ComPtr<ID3D12Resource> resource_;

        /// 要素数 1 なら「全体を 1 単位として追跡」、2 以上ならサブリソース個別追跡
        std::vector<D3D12_RESOURCE_STATES> states_{ D3D12_RESOURCE_STATE_COMMON };
    };
}
