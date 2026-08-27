#include "pch.h"
#include "CloudRenderer.h"

#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Cloud/Shader/CloudBindings.h"
#include "Graphics/Cloud/Shader/CloudPipelines.h"
#include "Graphics/Cloud/Render/CloudRenderContext.h"
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

        // ===== レイマーチ CS: BaseShapeNoise + SceneDepth → 半解像度 CloudBuffer =====
        Barrier::Transition(cmdList, res.cloudBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

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
            binder.Set(pass.bindings[B::gCloudOutput], res.cloudBuffer.uav.gpuHandle);
            binder.ValidateBeforeDraw(pass.bindings);
        }

        ctx.DispatchHalfRes();

        // 合成 CS が SRV として読めるよう遷移
        Barrier::UAV(cmdList, res.cloudBuffer);
        Barrier::Transition(cmdList, res.cloudBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        // ===== 合成 CS: CloudBuffer を SceneColor へ in-place 合成 =====
        Barrier::Transition(cmdList, sceneColor, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        {
            namespace B = CloudCompositeBind;
            const CloudComputePass& pass = (*ctx.pipelines)[CloudPass::Composite];
            ShaderBinder binder = pass.Begin(cmdList);
            binder.Set(pass.bindings[B::gCloud], ctx.cloudConstants);
            binder.Set(pass.bindings[B::gCloudBuffer], res.cloudBuffer.srv.gpuHandle);
            binder.Set(pass.bindings[B::gSceneDepth], depthSrvHandle);
            binder.Set(pass.bindings[B::gOutput], sceneColorUavHandle);
            binder.ValidateBeforeDraw(pass.bindings);
        }

        ctx.DispatchCompositeInPlace(sceneColor);

        Barrier::Transition(cmdList, res.cloudBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
}
