#include "pch.h"
#include "CloudSkyCubemapBaker.h"

#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Cloud/Shader/CloudBindings.h"
#include "Graphics/Cloud/Shader/CloudPipelines.h"
#include "Graphics/Cloud/Render/CloudRenderContext.h"
#include "Graphics/Cloud/Render/CloudStageScope.h"
#include "Graphics/Cloud/Resource/CloudResources.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"

namespace CoreEngine
{
    bool CloudSkyCubemapBaker::Bake(const CloudRenderContext& ctx)
    {
        const D3D12_GPU_DESCRIPTOR_HANDLE cubemapUav = ctx.atmosphere->GetSkyCubemapUAVHandle();
        if (cubemapUav.ptr == 0) {
            return false;
        }

        CloudResources& res = *ctx.resources;
        CloudStageScope stage(ctx, "Cloud Cubemap Capture");

        {
            namespace B = CloudCubemapCaptureBind;
            const CloudComputePass& pass = (*ctx.pipelines)[CloudPass::CubemapCapture];
            ShaderBinder binder = pass.Begin(ctx.cmdList);
            binder.Set(pass.bindings[B::gCloud], ctx.cloudConstants);
            binder.Set(pass.bindings[B::gAtmosphere], ctx.atmosphere->GetConstantBufferGPUAddress());
            binder.Set(pass.bindings[B::gBaseShapeNoise], res.baseShapeNoise.srv.gpuHandle);
            binder.Set(pass.bindings[B::gDetailNoise], res.detailNoise.srv.gpuHandle);
            binder.Set(pass.bindings[B::gWeatherMap], res.weatherMap.srv.gpuHandle);
            binder.Set(pass.bindings[B::gTransmittanceLUT], ctx.atmosphere->GetTransmittanceLUTSRVHandle());
            binder.Set(pass.bindings[B::gSkyViewLUT], ctx.atmosphere->GetSkyViewLUTSRVHandle());
            binder.Set(pass.bindings[B::gCloudPaintMap], res.weatherPaint.srv.gpuHandle);
            binder.Set(pass.bindings[B::gSkyCubemap], cubemapUav);
            binder.Set(pass.bindings[B::gCloudCube], res.cloudCube.uav.gpuHandle);
            binder.ValidateBeforeDraw(pass.bindings);
        }

        constexpr uint32_t kCubemapSize = AtmosphereManager::kSkyCubemapSize;
        ctx.cmdList->Dispatch((kCubemapSize + 7) / 8, (kCubemapSize + 7) / 8, 6);
        // 空キューブマップは後段のプリフィルタ（PrefilterSkyEnvironment）が SRV 遷移で同期する。
        // 雲だけのキューブは次の焼き込みが読むので、書き込みの完了をここで待たせる
        Barrier::UAV(ctx.cmdList, res.cloudCube);
        return true;
    }
}
