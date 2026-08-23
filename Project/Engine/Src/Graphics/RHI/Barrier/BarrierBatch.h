#pragma once

#include <d3d12.h>
#include <vector>

#include "Graphics/RHI/Resource/GpuResource.h"

namespace CoreEngine
{
    /// @brief 複数のバリアを蓄積して 1 回の ResourceBarrier で発行するバッチ
    /// @details 対象は `GpuResource`。ステートは GpuResource が持っているので
    ///          呼び出し側はステート変数を渡さない（渡せない）。
    ///
    ///          ステートの更新は **Transition() を呼んだ時点**で行う。
    ///          こうすると同じリソースに対して 1 バッチ内で A→B→C と積んでも
    ///          before が正しく連鎖する（旧実装は Flush 時に一括更新していたため
    ///          2 本目が A→C という誤ったバリアになった）。
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
        /// @details Debug ビルドで全バリアをログすると 60 秒の実行で 100MB を超えるため、
        ///          既定は OFF。バリア不整合（#527）を追うときだけ有効にする
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
        /// @details UAV バリアはステートを変えないので追跡の外にあっても安全。
        ///          加速構造バッファのように「生成後ずっと同じステート」のものに使う
        void UAVRaw(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* resource);
    }
}
