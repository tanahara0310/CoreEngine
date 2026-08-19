#include "pch.h"
#include "AtmosphereLUTPass.h"

#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Cloud/VolumetricCloudManager.h"
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

        ID3D12GraphicsCommandList* cmdList = context.cmdList;
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

        // ===== 空キューブマップ（スペキュラ IBL / 空・雲の映り込み） =====
        // Sky-View LUT 再生成時と雲が動いている間は毎フレーム、
        // 「空の焼き込み → 雲の前乗算合成 → GGX プリフィルタ」を実行する。
        // 雲ノイズは本パスの後で生成されるため、初回フレームだけ雲なしで焼かれる。
        auto* cloudManager = context.volumetricCloudManager;
        const bool cloudsReady = cloudManager
            && cloudManager->AreCloudsActive()
            && cloudManager->AreNoiseTexturesReady();
        if (context.atmosphereManager->ConsumeSkyEnvironmentDirty(cloudsReady, context.frameNumber)) {
            context.atmosphereManager->CaptureSkyEnvironment(cmdList);
            if (cloudsReady) {
                cloudManager->RenderCloudsToSkyCubemap(cmdList, context.atmosphereManager);
            }
            context.atmosphereManager->PrefilterSkyEnvironment(cmdList);
        }
    }
}
