#include "pch.h"
#include "CloudShadowMapRenderer.h"

#include "Graphics/Cloud/Shader/CloudBindings.h"
#include "Graphics/Cloud/Shader/CloudPipelines.h"
#include "Graphics/Cloud/Render/CloudRenderContext.h"
#include "Graphics/Cloud/Render/CloudStageScope.h"
#include "Graphics/Cloud/Resource/CloudResources.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"

namespace CoreEngine
{
    void CloudShadowMapRenderer::Render(const CloudRenderContext& ctx)
    {
        ID3D12GraphicsCommandList* cmdList = ctx.cmdList;
        CloudResources& res = *ctx.resources;

        // 書くのは gCloudShadow.rowOffset 行目から ctx.cloudShadowRowCount 行だけ
        // （行の分け方と範囲の決め方は VolumetricCloudManager::UpdateCloudShadowConstants）
        if (ctx.cloudShadowRowCount == 0) {
            return;
        }
        CloudStageScope stage(ctx, "Cloud ShadowMap");

        Barrier::Transition(cmdList, res.cloudShadowMap, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        {
            namespace B = CloudShadowMapBind;
            const CloudComputePass& pass = (*ctx.pipelines)[CloudPass::CloudShadowMap];
            ShaderBinder binder = pass.Begin(cmdList);
            binder.Set(pass.bindings[B::gCloud], ctx.cloudConstants);
            binder.Set(pass.bindings[B::gCloudShadow], ctx.cloudShadowConstants);
            binder.Set(pass.bindings[B::gBaseShapeNoise], res.baseShapeNoise.srv.gpuHandle);
            binder.Set(pass.bindings[B::gWeatherMap], res.weatherMap.srv.gpuHandle);
            binder.Set(pass.bindings[B::gCloudPaintMap], res.weatherPaint.srv.gpuHandle);
            binder.Set(pass.bindings[B::gCloudShadowMap], res.cloudShadowMap.uav.gpuHandle);
            binder.ValidateBeforeDraw(pass.bindings);
        }

        cmdList->Dispatch(
            (CloudResources::kCloudShadowMapSize + 7) / 8,
            (ctx.cloudShadowRowCount + 7) / 8,
            1);

        // Deferred ライティングとゴッドレイが SRV として読む
        Barrier::UAV(cmdList, res.cloudShadowMap);
        Barrier::Transition(cmdList, res.cloudShadowMap,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
}
