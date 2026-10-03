#pragma once

#include <d3d12.h>
#include <cstdint>
#include <string>
#include <vector>

#include "RenderGraph.h"

namespace CoreEngine
{
    /// @brief スナップショット時点での論理リソース 1 件
    struct RenderGraphSnapshotResource {
        std::string name;
        D3D12_GPU_DESCRIPTOR_HANDLE srvHandle{};                          ///< プレビュー用（0 なら SRV 無し）
        D3D12_RESOURCE_STATES stateAtCapture = D3D12_RESOURCE_STATE_COMMON;
        uint32_t version = 0;      ///< Write 宣言の回数。0 なら誰も書いていない
        uint32_t writerCount = 0;  ///< このリソースへ書くパス数
        uint32_t readerCount = 0;  ///< このリソースを読むパス数
        bool resolved = false;     ///< 実リソースと状態参照が解決できたか
    };

    /// @brief スナップショット内のパス 1 件
    /// @details RenderGraphPass に「実体がフレームを跨いで生存するか」の情報を足したもの。
    ///          PostEffect の分解ノードは毎フレーム作り直されるため、
    ///          ポーズ中にポインタを触ると解放済みメモリへのアクセスになる。
    struct RenderGraphSnapshotPass : RenderGraphPass {
        bool transient = false; ///< true なら renderPass は nullptr（有効/無効の切り替え不可）
    };

    /// @brief RenderGraph 構築・実行結果の複製
    /// @details RenderGraph は毎フレーム Reset されるので、エディタが眺めるために値としてコピーを取る。
    ///          GPU 計測スロットはパス名で引く（RenderGraph::Execute と同じ名前）。
    struct RenderGraphSnapshot {
        uint64_t frameNumber = 0;

        std::vector<RenderGraphSnapshotPass> passes;
        std::vector<uint32_t> executionOrder;
        std::vector<RenderGraphSnapshotResource> resources;

        /// @brief 論理リソース名からスナップショット内のエントリを引く
        /// @param resourceName 論理リソース名
        /// @return 見つかればそのポインタ。無ければ nullptr
        const RenderGraphSnapshotResource* FindResource(const std::string& resourceName) const
        {
            for (const RenderGraphSnapshotResource& resource : resources) {
                if (resource.name == resourceName) {
                    return &resource;
                }
            }
            return nullptr;
        }
    };
}
