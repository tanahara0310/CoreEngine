#include "pch.h"
#include "ShadowMapPass.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Render/Render.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/Shadow/ShadowMapManager.h"

namespace CoreEngine
{
    void ShadowMapPass::Execute(const RenderContext& context)
    {
        if (!context.renderManager || !context.lightManager || !context.shadowMapManager) {
            return;
        }

        // ShadowパスでスキニングのMatrixPalette SRVを使うため、
        // このパス自身でSRVヒープを明示的に設定する。
        if (context.dxCommon) {
            if (auto* cmdList = context.dxCommon->GetCommandList()) {
                ID3D12DescriptorHeap* heaps[] = { context.dxCommon->GetSRVHeap() };
                cmdList->SetDescriptorHeaps(1, heaps);
            }
        }

        // ライトVP行列を計算してRenderManagerに設定
        Matrix4x4 lightVP = context.lightManager->CalculateMainDirectionalLightViewProjection(
            sceneCenter_, sceneRadius_);
        context.renderManager->SetLightViewProjection(lightVP);

        // シャドウマップパスの実行
        context.renderManager->DrawShadowPass();

        if (context.frameBlackboard) {
            // Blackboard にシャドウマップ出力の実リソースと現在状態参照を公開する。
            D3D12_RESOURCE_STATES& currentState = context.shadowMapManager->GetCurrentState();
            context.frameBlackboard->SetResource(
                FrameBlackboard::ShadowMap,
                context.shadowMapManager->GetSRVHandle(),
                context.shadowMapManager->GetShadowMapResource(),
                &currentState);
        }
    }
}
