#include "ShadowMapPass.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Render/Render.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Light/LightManager.h"

namespace CoreEngine
{
    void ShadowMapPass::Execute(const RenderContext& context)
    {
        if (!context.renderManager || !context.lightManager) {
            return;
        }

        // ライトVP行列を計算してRenderManagerに設定
        Matrix4x4 lightVP = context.lightManager->CalculateMainDirectionalLightViewProjection(
            sceneCenter_, sceneRadius_);
        context.renderManager->SetLightViewProjection(lightVP);

        // シャドウマップパスの実行
        context.renderManager->RenderShadowMapPass();
    }
}
