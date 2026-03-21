#include "DeferredLightingPass.h"

#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/PostEffect/Effect/DeferredLighting.h"
#include "Graphics/PostEffect/PostEffectManager.h"
#include "Graphics/PostEffect/PostEffectNames.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Graphics/Shadow/ShadowMapManager.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/Model/ModelRenderer.h"

namespace CoreEngine
{
    void DeferredLightingPass::Execute(const RenderContext& context)
    {
        // 必須コンポーネントの確認
        if (!context.postEffectManager || !context.renderTargetManager
            || !context.gBufferManager   || !context.dxCommon) {
            output_.Reset();
            return;
        }

        // 出力先 RenderTarget を名前で取得
        auto* target = context.renderTargetManager->GetRenderTarget(targetName_);
        if (!target) {
            output_.Reset();
            return;
        }

        // DeferredLighting エフェクトを取得
        auto* deferredLighting = context.postEffectManager->GetEffect<DeferredLighting>(
            PostEffectNames::DeferredLighting);
        if (!deferredLighting) {
            output_.Reset();
            return;
        }

        // ===== G-Buffer SRV を全ターゲット分セット =====
        deferredLighting->SetNormalRoughnessHandle(
            context.gBufferManager->GetSRVHandle(GBufferManager::Target::NormalRoughness));
        deferredLighting->SetEmissiveMetallicHandle(
            context.gBufferManager->GetSRVHandle(GBufferManager::Target::EmissiveMetallic));
        deferredLighting->SetWorldPositionHandle(
            context.gBufferManager->GetSRVHandle(GBufferManager::Target::WorldPosition));

        // ===== カメラ CBV アドレス（GBufferPass で設定済み） =====
        if (context.renderManager) {
            // ModelRenderer が GBufferPass 時点でカメラを保持している
            if (auto* modelRenderer = dynamic_cast<ModelRenderer*>(
                    context.renderManager->GetRenderer(RenderPassType::Model))) {
                deferredLighting->SetCameraCBVAddress(modelRenderer->GetCameraCBVAddress());
            }

            // IBL ハンドルを RenderManager から取得してセット
            deferredLighting->SetIrradianceMapHandle(context.renderManager->GetIrradianceMapHandle());
            deferredLighting->SetPrefilteredMapHandle(context.renderManager->GetPrefilteredMapHandle());
            deferredLighting->SetBRDFLUTHandle(context.renderManager->GetBRDFLUTHandle());

            // シーン共通 IBL 回転を転送（スカイボックス回転と連動）
            deferredLighting->SetEnvironmentRotation(context.renderManager->GetIBLRotation());
        }

        // ===== LightManager を渡す（4種ライトバインド） =====
        deferredLighting->SetLightManager(context.lightManager);

        // ===== IBL パラメータを GPU バッファに書き込む =====
        deferredLighting->UpdateIBLParams();

        // ===== Shadow Map と LightViewProjection の設定 =====
        if (context.shadowMapManager) {
            // シャドウマップ SRV をセット
            deferredLighting->SetShadowMapHandle(context.shadowMapManager->GetSRVHandle());

            // ライト VP 行列を GPU バッファに書き込む（毎フレーム更新）
            deferredLighting->UpdateLightViewProjection(
                context.shadowMapManager->GetLightViewProjection());
        }

        // ===== ライティングパスを実行 =====
        auto* cmdList = context.dxCommon->GetCommandList();
        target->Begin(cmdList);
        deferredLighting->Draw(context.gBufferManager->GetSRVHandle(GBufferManager::Target::AlbedoAO));
        target->End(cmdList);

        // 結果を次のパスに渡す
        output_.srvHandle = target->GetSRVHandle();
        output_.resource  = target->GetResource();
        output_.isValid   = true;
    }
}

