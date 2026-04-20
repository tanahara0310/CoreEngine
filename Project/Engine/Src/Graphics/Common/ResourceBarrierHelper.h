#pragma once

#include <d3d12.h>

namespace CoreEngine
{
    /// @brief D3D12 リソースバリアのヘルパークラス
    /// @details currentState の追跡と正しいバリア発行を一元化する。
    ///          既に目標ステートにある場合はバリアを発行しない（冗長バリア防止）。
    class ResourceBarrierHelper {
    public:
        /// @brief リソースを targetState に遷移させる
        /// @details currentState == targetState の場合はバリアをスキップする。
        ///          バリア発行後に currentState を targetState に自動更新する。
        /// @param cmdList      コマンドリスト
        /// @param resource     対象リソース
        /// @param currentState 現在のリソースステート（バリア後に自動更新される）
        /// @param targetState  遷移先ステート
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
}
