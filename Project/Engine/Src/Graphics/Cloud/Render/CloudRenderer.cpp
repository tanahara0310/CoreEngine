#include "pch.h"
#include "CloudRenderer.h"

#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Cloud/Shader/CloudBindings.h"
#include "Graphics/Cloud/Shader/CloudPipelines.h"
#include "Graphics/Cloud/Render/CloudRenderContext.h"
#include "Graphics/Cloud/Render/CloudStageScope.h"
#include "Graphics/Cloud/Resource/CloudResources.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"

namespace CoreEngine
{
    void CloudRenderer::Render(const CloudRenderContext& ctx,
                               GpuResource& sceneColor,
                               D3D12_GPU_DESCRIPTOR_HANDLE sceneColorUavHandle,
                               D3D12_GPU_DESCRIPTOR_HANDLE depthSrvHandle)
    {
        ID3D12GraphicsCommandList* cmdList = ctx.cmdList;
        CloudResources& res = *ctx.resources;
        CloudGpuTexture& target = res.CurrentCloudBuffer();
        CloudGpuTexture& history = res.HistoryCloudBuffer();

        // ===== レイマーチ CS: BaseShapeNoise + SceneDepth → 半解像度 CloudBuffer =====
        {
            CloudStageScope stage(ctx, "Cloud RayMarch");

            Barrier::Transition(cmdList, target, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            Barrier::Transition(cmdList, history, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

            {
                namespace B = CloudRayMarchBind;
                const CloudComputePass& pass = (*ctx.pipelines)[CloudPass::RayMarch];
                ShaderBinder binder = pass.Begin(cmdList);
                binder.Set(pass.bindings[B::gCloud], ctx.cloudConstants);
                // 大気散乱の定数バッファと LUT（太陽色・アンビエントの単一情報源）
                binder.Set(pass.bindings[B::gAtmosphere], ctx.atmosphere->GetConstantBufferGPUAddress());
                binder.Set(pass.bindings[B::gBaseShapeNoise], res.baseShapeNoise.srv.gpuHandle);
                binder.Set(pass.bindings[B::gDetailNoise], res.detailNoise.srv.gpuHandle);
                binder.Set(pass.bindings[B::gWeatherMap], res.weatherMap.srv.gpuHandle);
                binder.Set(pass.bindings[B::gSceneDepth], depthSrvHandle);
                binder.Set(pass.bindings[B::gTransmittanceLUT], ctx.atmosphere->GetTransmittanceLUTSRVHandle());
                binder.Set(pass.bindings[B::gSkyViewLUT], ctx.atmosphere->GetSkyViewLUTSRVHandle());
                binder.Set(pass.bindings[B::gCloudHistory], history.srv.gpuHandle);
                // 空気遠近は不透明ジオメトリと同じ LUT を引く（地平線で霞み方が揃う）
                binder.Set(pass.bindings[B::gCameraVolumeLUT], ctx.atmosphere->GetCameraVolumeLUTSRVHandle());
                binder.Set(pass.bindings[B::gCloudOutput], target.uav.gpuHandle);
                binder.ValidateBeforeDraw(pass.bindings);
            }

            ctx.DispatchHalfRes();

            // 合成 CS が SRV として読めるよう遷移
            Barrier::UAV(cmdList, target);
            Barrier::Transition(cmdList, target, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }

        // ===== 合成 CS: CloudBuffer を SceneColor へ in-place 合成 =====
        {
            CloudStageScope stage(ctx, "Cloud Composite");

            Barrier::Transition(cmdList, sceneColor, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            {
                namespace B = CloudCompositeBind;
                const CloudComputePass& pass = (*ctx.pipelines)[CloudPass::Composite];
                ShaderBinder binder = pass.Begin(cmdList);
                binder.Set(pass.bindings[B::gCloud], ctx.cloudConstants);
                binder.Set(pass.bindings[B::gCloudBuffer], target.srv.gpuHandle);
                binder.Set(pass.bindings[B::gSceneDepth], depthSrvHandle);
                binder.Set(pass.bindings[B::gOutput], sceneColorUavHandle);
                binder.ValidateBeforeDraw(pass.bindings);
            }

            ctx.DispatchCompositeInPlace(sceneColor);

            // 今フレームの結果は次フレームの履歴になるので SRV 状態のまま残す
        }
    }
}
