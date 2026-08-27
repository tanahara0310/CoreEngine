#include "pch.h"
#include "VolumetricCloudNoisePass.h"

#include "Graphics/Cloud/VolumetricCloudManager.h"
#include "Graphics/RHI/GraphicsCore.h"

namespace CoreEngine
{
    void VolumetricCloudNoisePass::Execute(const RenderContext& context)
    {
        if (!context.dxCommon || !context.volumetricCloudManager) {
            return;
        }

        // 雲を使うシーン（Update() を呼ぶシーン）でのみノイズを生成する。
        // これを判定しないと、雲非対応シーンでもノイズが生成され描画へ漏れ出す。
        if (!context.volumetricCloudManager->AreCloudsActive()) {
            return;
        }

        ID3D12GraphicsCommandList* cmdList = context.cmdList;
        if (!cmdList) {
            return;
        }

        // ノイズ生成 compute はディスクリプタテーブルを使うため、実行順に依存せず
        // 動作するようパス自身が SRV ヒープをバインドする（パス分離契約 2）。
        // SRV ヒープはフレーム先頭で CommandContext が 1 回バインドする（個別バインドは不要）

        // ダーティフラグが立っている場合のみ内部で再生成される
        context.volumetricCloudManager->GenerateNoiseTexturesIfNeeded(cmdList, context.gpuProfiler);
    }
}
