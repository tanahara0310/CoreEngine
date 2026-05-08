#include "SSAOPass.h"

#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/PostEffect/Effect/SSAO.h"
#include "Graphics/PostEffect/Effect/SSAOBlur.h"
#include "Graphics/PostEffect/PostEffectManager.h"
#include "Graphics/PostEffect/PostEffectNames.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Graphics/Render/RenderManager.h"

namespace CoreEngine
{
    void SSAOPass::Execute(const RenderContext& context)
    {
        if (!context.postEffectManager || !context.renderTargetManager
            || !context.gBufferManager || !context.dxCommon) {
            output_.Reset();
            return;
        }

        auto* ssao = context.postEffectManager->GetEffect<SSAO>(PostEffectNames::SSAO);
        auto* ssaoBlur = context.postEffectManager->GetEffect<SSAOBlur>(PostEffectNames::SSAOBlur);
        if (!ssao || !ssao->IsEnabled()) {
            output_.Reset();
            return;
        }

        auto* ssaoTarget     = context.renderTargetManager->GetRenderTarget(ssaoTargetName_);
        auto* ssaoBlurTarget = context.renderTargetManager->GetRenderTarget(ssaoBlurTargetName_);
        if (!ssaoTarget || !ssaoBlurTarget) {
            output_.Reset();
            return;
        }

        // GBuffer の幅/高さ
        const float w = static_cast<float>(context.gBufferManager->GetWidth());
        const float h = static_cast<float>(context.gBufferManager->GetHeight());
        ssao->SetScreenSize(w, h);

        // カメラ行列を RenderManager から取得して定数バッファに書き込む
        if (context.renderManager) {
            const Matrix4x4& view = context.renderManager->GetViewMatrix();
            const Matrix4x4& proj = context.renderManager->GetProjectionMatrix();
            ssao->SetViewMatrix(reinterpret_cast<const float*>(&view));
            ssao->SetProjectionMatrix(reinterpret_cast<const float*>(&proj));
        }

        // NormalRoughness / WorldPosition SRV を SSAO にセット
        ssao->SetNormalRoughnessHandle(
            context.gBufferManager->GetSRVHandle(GBufferManager::Target::NormalRoughness));
        ssao->SetWorldPositionHandle(
            context.gBufferManager->GetSRVHandle(GBufferManager::Target::WorldPosition));

        ssao->UpdateConstantBuffer();

        auto* cmdList = context.dxCommon->GetCommandList();

        // ----- SSAO 生成 -----
        ssaoTarget->Begin(cmdList);
        // t0 = gTexture (= NormalRoughness)  PostEffectBase 規約によりここに渡す
        ssao->Draw(context.gBufferManager->GetSRVHandle(GBufferManager::Target::NormalRoughness));
        ssaoTarget->End(cmdList);

        // ----- SSAO ブラー -----
        if (ssaoBlur && ssaoBlur->IsEnabled()) {
            ssaoBlur->SetWorldPositionHandle(
                context.gBufferManager->GetSRVHandle(GBufferManager::Target::WorldPosition));
            ssaoBlur->SetScreenSize(w, h);
            ssaoBlur->UpdateConstantBuffer();

            ssaoBlurTarget->Begin(cmdList);
            ssaoBlur->Draw(ssaoTarget->GetSRVHandle());
            ssaoBlurTarget->End(cmdList);

            output_.srvHandle = ssaoBlurTarget->GetSRVHandle();
            output_.resource  = ssaoBlurTarget->GetResource();
        } else {
            output_.srvHandle = ssaoTarget->GetSRVHandle();
            output_.resource  = ssaoTarget->GetResource();
        }

        output_.isValid = true;
    }
}
