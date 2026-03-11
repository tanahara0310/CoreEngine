#include "ShadowMapPass.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Render/Render.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/Model/Model.h"

namespace CoreEngine
{
    void ShadowMapPass::Execute(const RenderContext& context)
    {
        if (!context.renderManager || !context.lightManager) {
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

        Model::SetTransformBufferSlot(Model::TransformBufferSlot::Shadow);

        // シャドウマップパスの実行
        context.renderManager->DrawShadowPass();
    }
}
