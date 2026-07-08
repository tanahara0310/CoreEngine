#include "pch.h"
#include "AtmosphereLUTPass.h"

#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Common/DirectXCommon.h"

namespace CoreEngine
{
    void AtmosphereLUTPass::Execute(const RenderContext& context)
    {
        if (!context.dxCommon || !context.atmosphereManager) {
            return;
        }

        // 大気を使うシーン（Update() を呼ぶシーン）でのみ LUT を生成する。
        // これを判定しないと、大気非対応シーンでも LUT が生成され lutsGenerated_ が
        // 立ち続け、AerialPerspectivePass が全シーンの SceneColor を上書きしてしまう。
        if (!context.atmosphereManager->IsAtmosphereActive()) {
            return;
        }

        ID3D12GraphicsCommandList* cmdList = context.dxCommon->GetCommandList();
        if (!cmdList) {
            return;
        }

        // LUT 生成 compute はディスクリプタテーブルを使うため、実行順に依存せず
        // 動作するようパス自身が SRV ヒープをバインドする（パス分離契約 2:
        // 先行パスが残した状態への暗黙依存を持たない）。
        // ※ これを行わずフレーム先頭で実行するとヒープ未バインドでクラッシュする。
        ID3D12DescriptorHeap* descriptorHeaps[] = { context.dxCommon->GetSRVHeap() };
        cmdList->SetDescriptorHeaps(1, descriptorHeaps);

        // ダーティフラグが立っている場合のみ内部で再計算される
        context.atmosphereManager->GenerateLUTsIfNeeded(cmdList);
    }
}
