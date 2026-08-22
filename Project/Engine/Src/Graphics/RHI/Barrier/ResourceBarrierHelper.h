#pragma once

#include <d3d12.h>
#include <vector>

namespace CoreEngine
{
    /// @brief D3D12 リソースバリアのヘルパークラス
    /// @details currentState の追跡と正しいバリア発行を一元化する。
    ///          既に目標ステートにある場合はバリアを発行しない（冗長バリア防止）。
    class ResourceBarrierHelper {
    public:
        /// @brief リソースを targetState に遷移させる
        /// @param currentState 現在のリソースステート（同一ならスキップし、遷移後は自動更新される）
        static void Transition(
            ID3D12GraphicsCommandList* cmdList,
            ID3D12Resource* resource,
            D3D12_RESOURCE_STATES& currentState,
            D3D12_RESOURCE_STATES targetState);

        /// @brief UAV バリアを発行する（UAV への書き込み完了を後続の読み取りに保証する）
        /// @param cmdList   コマンドリスト
        /// @param resource  対象リソース（nullptr で全 UAV バリア）
        static void UAV(
            ID3D12GraphicsCommandList* cmdList,
            ID3D12Resource* resource = nullptr);
    };

    // ---------------------------------------------------------------

    /// @brief 複数のバリアを蓄積して一括発行するバッチクラス
    /// @details Add()/AddUAV() で追加し、Flush() で一括発行する。
    ///          デストラクタでも未発行のバリアを自動的に Flush する。
    class ResourceBarrierBatch {
    public:
        /// @param cmdList バリアを発行するコマンドリスト
        explicit ResourceBarrierBatch(ID3D12GraphicsCommandList* cmdList);

        /// @brief デストラクタ — 未発行バリアを自動 Flush する
        ~ResourceBarrierBatch();

        // コピー禁止
        ResourceBarrierBatch(const ResourceBarrierBatch&) = delete;
        ResourceBarrierBatch& operator=(const ResourceBarrierBatch&) = delete;

        /// @brief トランジションバリアを追加する（冗長ステートはスキップ）
        /// @param resource     対象リソース
        /// @param currentState 現在のリソースステート（Flush 後に自動更新される）
        /// @param targetState  遷移先ステート
        void Add(
            ID3D12Resource* resource,
            D3D12_RESOURCE_STATES& currentState,
            D3D12_RESOURCE_STATES targetState);

        /// @brief UAV バリアを追加する
        /// @param resource 対象リソース（nullptr で全 UAV バリア）
        void AddUAV(ID3D12Resource* resource = nullptr);

        /// @brief 蓄積されたバリアを一括発行してリストをクリアする
        void Flush();

    private:
        ID3D12GraphicsCommandList* cmdList_;

        // pending バリアリストと、Flush 後のステート更新用ペア
        struct PendingTransition {
            D3D12_RESOURCE_BARRIER barrier;
            D3D12_RESOURCE_STATES* stateRef; // Flush 後に targetState へ書き戻す
        };

        std::vector<PendingTransition> pendingTransitions_;
        std::vector<D3D12_RESOURCE_BARRIER> pendingUAVs_;
    };

    // ---------------------------------------------------------------

    /// @brief スコープ退出時にリソースステートを元に戻す RAII ガード
    /// @details 構築時に targetState へ即時遷移し、デストラクタで元ステートへ自動復元する。
    ///          一時的に状態を変えて処理を行い、必ず元に戻す用途に使用する。
    class ScopedResourceBarrier {
    public:
        /// @param cmdList      コマンドリスト
        /// @param resource     対象リソース
        /// @param currentState 現在のリソースステート（RAII 完了後に元ステートへ更新）
        /// @param targetState  スコープ内で使用するステート
        ScopedResourceBarrier(
            ID3D12GraphicsCommandList* cmdList,
            ID3D12Resource* resource,
            D3D12_RESOURCE_STATES& currentState,
            D3D12_RESOURCE_STATES targetState);

        /// @brief デストラクタ — 元ステートへ自動復元する
        ~ScopedResourceBarrier();

        // コピー・ムーブ禁止
        ScopedResourceBarrier(const ScopedResourceBarrier&) = delete;
        ScopedResourceBarrier& operator=(const ScopedResourceBarrier&) = delete;

    private:
        ID3D12GraphicsCommandList* cmdList_;
        ID3D12Resource* resource_;
        D3D12_RESOURCE_STATES& currentState_; // 呼び出し元のステート変数への参照
        D3D12_RESOURCE_STATES originalState_;
    };

} // namespace CoreEngine
