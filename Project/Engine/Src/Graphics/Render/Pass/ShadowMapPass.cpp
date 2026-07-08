#include "pch.h"
#include "ShadowMapPass.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Render/Render.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/Shadow/ShadowMapManager.h"
#include "Graphics/Render/RenderGraph.h"

namespace CoreEngine
{
    void ShadowMapPass::DeclareResources(RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        // 主方向ライトの深度を書き出す先行パス。
        builder.Write(FrameBlackboard::ShadowMap, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    }

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

        // ShadowMap は RegisterFrameResources で同一の実体が登録済みのため、
        // 実行中の Blackboard 再登録は行わない（パス分離契約 3）。
    }
}
