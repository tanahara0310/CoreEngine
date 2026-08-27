#include "pch.h"
#include "GodRayRenderer.h"

#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Cloud/Shader/CloudBindings.h"
#include "Graphics/Cloud/Shader/CloudPipelines.h"
#include "Graphics/Cloud/Render/CloudRenderContext.h"
#include "Graphics/Cloud/Render/CloudStageScope.h"
#include "Graphics/Cloud/Resource/CloudResources.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"

namespace CoreEngine
{
    void GodRayRenderer::Render(const CloudRenderContext& ctx,
                                GpuResource& sceneColor,
                                D3D12_GPU_DESCRIPTOR_HANDLE sceneColorUavHandle,
                                D3D12_GPU_DESCRIPTOR_HANDLE depthSrvHandle)
    {
        ID3D12GraphicsCommandList* cmdList = ctx.cmdList;
        CloudResources& res = *ctx.resources;

        // ===== 雲シャドウマップ生成 =====
        // 風の移流・太陽移動・カメラ追従で毎フレーム変わるため、雲アクティブ中は毎回焼き直す
        // （1024²×24 サンプルの cheap 密度で Transmittance LUT 生成より軽い）。
        {
            CloudStageScope stage(ctx, "Cloud ShadowMap");

            Barrier::Transition(cmdList, res.cloudShadowMap, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            {
                namespace B = CloudShadowMapBind;
                const CloudComputePass& pass = (*ctx.pipelines)[CloudPass::CloudShadowMap];
                ShaderBinder binder = pass.Begin(cmdList);
                binder.Set(pass.bindings[B::gCloud], ctx.cloudConstants);
                binder.Set(pass.bindings[B::gGodRay], ctx.godRayConstants);
                binder.Set(pass.bindings[B::gBaseShapeNoise], res.baseShapeNoise.srv.gpuHandle);
                binder.Set(pass.bindings[B::gWeatherMap], res.weatherMap.srv.gpuHandle);
                binder.Set(pass.bindings[B::gCloudShadowMap], res.cloudShadowMap.uav.gpuHandle);
                binder.ValidateBeforeDraw(pass.bindings);
            }

            cmdList->Dispatch(
                (CloudResources::kCloudShadowMapSize + 7) / 8,
                (CloudResources::kCloudShadowMapSize + 7) / 8,
                1);

            // マーチ CS が SRV として読めるよう遷移
            Barrier::UAV(cmdList, res.cloudShadowMap);
            Barrier::Transition(cmdList, res.cloudShadowMap, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }

        // ===== ゴッドレイマーチ CS: 遮蔽差分を半解像度で積分 =====
        // 雲透過率（cloudBuffer.a）で差分をスケールするため SRV として読む
        // （CloudRenderer が末尾で UAV 状態へ戻している）
        {
            CloudStageScope stage(ctx, "GodRay March");

            Barrier::Transition(cmdList, res.cloudBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            Barrier::Transition(cmdList, res.godRayBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            {
                namespace B = GodRayMarchBind;
                const CloudComputePass& pass = (*ctx.pipelines)[CloudPass::GodRayMarch];
                ShaderBinder binder = pass.Begin(cmdList);
                binder.Set(pass.bindings[B::gGodRay], ctx.godRayConstants);
                binder.Set(pass.bindings[B::gAtmosphere], ctx.atmosphere->GetConstantBufferGPUAddress());
                binder.Set(pass.bindings[B::gCloudShadowMap], res.cloudShadowMap.srv.gpuHandle);
                binder.Set(pass.bindings[B::gTransmittanceLUT], ctx.atmosphere->GetTransmittanceLUTSRVHandle());
                binder.Set(pass.bindings[B::gSceneDepth], depthSrvHandle);
                binder.Set(pass.bindings[B::gCloudBuffer], res.cloudBuffer.srv.gpuHandle);
                binder.Set(pass.bindings[B::gGodRayOutput], res.godRayBuffer.uav.gpuHandle);
                binder.ValidateBeforeDraw(pass.bindings);
            }

            ctx.DispatchHalfRes();

            // 合成 CS が SRV として読めるよう遷移
            Barrier::UAV(cmdList, res.godRayBuffer);
            Barrier::Transition(cmdList, res.godRayBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }

        // ===== 合成 CS: Δ輝度を SceneColor へ in-place 加算 =====
        {
            CloudStageScope stage(ctx, "GodRay Composite");

            Barrier::Transition(cmdList, sceneColor, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            {
                namespace B = GodRayCompositeBind;
                const CloudComputePass& pass = (*ctx.pipelines)[CloudPass::GodRayComposite];
                ShaderBinder binder = pass.Begin(cmdList);
                binder.Set(pass.bindings[B::gGodRay], ctx.godRayConstants);
                binder.Set(pass.bindings[B::gGodRayBuffer], res.godRayBuffer.srv.gpuHandle);
                binder.Set(pass.bindings[B::gOutput], sceneColorUavHandle);
                binder.ValidateBeforeDraw(pass.bindings);
            }

            ctx.DispatchCompositeInPlace(sceneColor);

            Barrier::Transition(cmdList, res.godRayBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            Barrier::Transition(cmdList, res.cloudShadowMap, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            Barrier::Transition(cmdList, res.cloudBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
    }
}
