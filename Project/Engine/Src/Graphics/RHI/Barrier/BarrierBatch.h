#pragma once

#include <d3d12.h>
#include <vector>

#include "Graphics/RHI/Resource/GpuResource.h"

namespace CoreEngine
{
    /// @brief 複数のバリアを蓄積して 1 回の ResourceBarrier で発行するバッチ
    /// @details ステートの更新は Transition() を呼んだ時点で行うので、
    ///          1 バッチ内で A→B→C と積んでも before が正しく連鎖する
    class BarrierBatch {
    public:
        /// @param cmdList バリアを発行するコマンドリスト
        explicit BarrierBatch(ID3D12GraphicsCommandList* cmdList);

        /// @brief デストラクタ — 未発行バリアを自動 Flush する
        ~BarrierBatch();

        BarrierBatch(const BarrierBatch&) = delete;
        BarrierBatch& operator=(const BarrierBatch&) = delete;

        /// @brief 遷移バリアを追加する（既に目標ステートならスキップ）
        /// @param resource    対象リソース（ステートは内部で更新される）
        /// @param after       遷移先ステート
        /// @param subresource kAllSubresources なら全体。個別追跡しているリソースは添字指定できる
        void Transition(GpuResource& resource,
                        D3D12_RESOURCE_STATES after,
                        uint32_t subresource = kAllSubresources);

        /// @brief 特定リソースの UAV バリアを追加する
        void UAV(GpuResource& resource);

        /// @brief 全 UAV に対するバリアを追加する
        void UAVAll();

        /// @brief 蓄積したバリアを一括発行する
        void Flush();

        bool Empty() const noexcept { return barriers_.empty(); }

        /// @brief バリアの逐次ログを有効化する（既定 OFF）
        /// @details 出力量が多いので、バリア不整合（#527）を追うときだけ有効にする
        static void SetLoggingEnabled(bool enabled) noexcept;
        static bool IsLoggingEnabled() noexcept;

    private:
        ID3D12GraphicsCommandList* cmdList_ = nullptr;
        std::vector<D3D12_RESOURCE_BARRIER> barriers_;
    };

    /// @brief バッチを組まずに 1 本だけバリアを張るショートカット
    namespace Barrier
    {
        /// @brief 単発の遷移バリア（既に目標ステートならスキップ）
        void Transition(ID3D12GraphicsCommandList* cmdList,
                        GpuResource& resource,
                        D3D12_RESOURCE_STATES after,
                        uint32_t subresource = kAllSubresources);

        /// @brief 単発の UAV バリア
        void UAV(ID3D12GraphicsCommandList* cmdList, GpuResource& resource);

        /// @brief 全 UAV に対するバリア
        void UAVAll(ID3D12GraphicsCommandList* cmdList);

        /// @brief GpuResource で保持していないリソースへの UAV バリア
        /// @details UAV バリアはステートを変えないので追跡の外にあっても安全
        void UAVRaw(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* resource);
    }
}
