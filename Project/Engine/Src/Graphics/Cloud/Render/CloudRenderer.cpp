#include "pch.h"
#include "CloudRenderer.h"

#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Cloud/Shader/CloudBindings.h"
#include "Graphics/Cloud/Shader/CloudPipelines.h"
#include "Graphics/Cloud/Render/CloudRenderContext.h"
#include "Graphics/Cloud/Render/CloudStageScope.h"
#include "Graphics/Cloud/Resource/CloudResources.h"
#include "Graphics/Cloud/Settings/CloudSettings.h"
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
        CloudGpuTexture& targetDepth = res.CurrentCloudDepthBuffer();
        CloudGpuTexture& historyDepth = res.HistoryCloudDepthBuffer();

        // ===== レイマーチ CS: BaseShapeNoise + SceneDepth → 半解像度 CloudBuffer =====
        // ブロック雲（Voxel / Minecraft）は固定ステップではなく DDA でボクセルをたどり、半解像度へ直接書く。
        // 写実の雲はセルごとに 1 画素だけ撃ち、組み立て CS が履歴と合わせて半解像度へ広げる。
        // 出力形式（前乗算アルファ）が同じなので、以降の合成とゴッドレイは共通のまま。
        const bool useVoxelMarch = CloudStyleUsesVoxelMarch(ctx.styleIndex);
        {
            CloudStageScope stage(ctx, useVoxelMarch ? "Cloud VoxelMarch" : "Cloud RayMarch");

            if (useVoxelMarch) {
                Barrier::Transition(cmdList, target, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            } else {
                Barrier::Transition(cmdList, res.traceColor, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                Barrier::Transition(cmdList, res.traceDepth, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            }

            if (useVoxelMarch) {
                namespace B = CloudVoxelMarchBind;
                const CloudComputePass& pass = (*ctx.pipelines)[CloudPass::VoxelRayMarch];
                ShaderBinder binder = pass.Begin(cmdList);
                binder.Set(pass.bindings[B::gCloud], ctx.cloudConstants);
                binder.Set(pass.bindings[B::gAtmosphere], ctx.atmosphere->GetConstantBufferGPUAddress());
                binder.Set(pass.bindings[B::gBaseShapeNoise], res.baseShapeNoise.srv.gpuHandle);
                binder.Set(pass.bindings[B::gWeatherMap], res.weatherMap.srv.gpuHandle);
                binder.Set(pass.bindings[B::gCloudPaintMap], res.weatherPaint.srv.gpuHandle);
                binder.Set(pass.bindings[B::gSceneDepth], depthSrvHandle);
                binder.Set(pass.bindings[B::gTransmittanceLUT], ctx.atmosphere->GetTransmittanceLUTSRVHandle());
                binder.Set(pass.bindings[B::gSkyViewLUT], ctx.atmosphere->GetSkyViewLUTSRVHandle());
                binder.Set(pass.bindings[B::gCameraVolumeLUT], ctx.atmosphere->GetCameraVolumeLUTSRVHandle());
                binder.Set(pass.bindings[B::gCloudOutput], target.uav.gpuHandle);
                binder.ValidateBeforeDraw(pass.bindings);
            } else {
                namespace B = CloudRayMarchBind;
                const CloudComputePass& pass = (*ctx.pipelines)[CloudPass::RayMarch];
                ShaderBinder binder = pass.Begin(cmdList);
                binder.Set(pass.bindings[B::gCloud], ctx.cloudConstants);
                // 大気散乱の定数バッファと LUT（太陽色・アンビエントの単一情報源）
                binder.Set(pass.bindings[B::gAtmosphere], ctx.atmosphere->GetConstantBufferGPUAddress());
                binder.Set(pass.bindings[B::gBaseShapeNoise], res.baseShapeNoise.srv.gpuHandle);
                binder.Set(pass.bindings[B::gDetailNoise], res.detailNoise.srv.gpuHandle);
                binder.Set(pass.bindings[B::gWeatherMap], res.weatherMap.srv.gpuHandle);
                binder.Set(pass.bindings[B::gCloudPaintMap], res.weatherPaint.srv.gpuHandle);
                binder.Set(pass.bindings[B::gSceneDepth], depthSrvHandle);
                binder.Set(pass.bindings[B::gTransmittanceLUT], ctx.atmosphere->GetTransmittanceLUTSRVHandle());
                binder.Set(pass.bindings[B::gSkyViewLUT], ctx.atmosphere->GetSkyViewLUTSRVHandle());
                // 空気遠近は不透明ジオメトリと同じ LUT を引く（地平線で霞み方が揃う）
                binder.Set(pass.bindings[B::gCameraVolumeLUT], ctx.atmosphere->GetCameraVolumeLUTSRVHandle());
                binder.Set(pass.bindings[B::gTraceColor], res.traceColor.uav.gpuHandle);
                binder.Set(pass.bindings[B::gTraceDepth], res.traceDepth.uav.gpuHandle);
                binder.ValidateBeforeDraw(pass.bindings);
            }

            if (useVoxelMarch) {
                ctx.DispatchHalfRes();
                // 合成 CS が SRV として読めるよう遷移
                Barrier::UAV(cmdList, target);
                Barrier::Transition(cmdList, target, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            } else {
                ctx.DispatchTraceCells();
                Barrier::UAV(cmdList, res.traceColor);
                Barrier::UAV(cmdList, res.traceDepth);
                Barrier::Transition(cmdList, res.traceColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                Barrier::Transition(cmdList, res.traceDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            }
        }

        // ===== 組み立て CS: セルごとの結果 + 前フレームの雲 → 半解像度 CloudBuffer =====
        if (!useVoxelMarch) {
            CloudStageScope stage(ctx, "Cloud Reconstruct");

            Barrier::Transition(cmdList, target, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            Barrier::Transition(cmdList, targetDepth, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            Barrier::Transition(cmdList, history, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            Barrier::Transition(cmdList, historyDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

            {
                namespace B = CloudReconstructBind;
                const CloudComputePass& pass = (*ctx.pipelines)[CloudPass::Reconstruct];
                ShaderBinder binder = pass.Begin(cmdList);
                binder.Set(pass.bindings[B::gCloud], ctx.cloudConstants);
                binder.Set(pass.bindings[B::gSceneDepth], depthSrvHandle);
                binder.Set(pass.bindings[B::gTraceColor], res.traceColor.srv.gpuHandle);
                binder.Set(pass.bindings[B::gTraceDepth], res.traceDepth.srv.gpuHandle);
                binder.Set(pass.bindings[B::gCloudHistory], history.srv.gpuHandle);
                binder.Set(pass.bindings[B::gCloudDepthHistory], historyDepth.srv.gpuHandle);
                binder.Set(pass.bindings[B::gCloudOutput], target.uav.gpuHandle);
                binder.Set(pass.bindings[B::gCloudDepthOutput], targetDepth.uav.gpuHandle);
                binder.ValidateBeforeDraw(pass.bindings);
            }

            ctx.DispatchHalfRes();

            // 合成 CS とゴッドレイが SRV として読み、次フレームは履歴として読む
            Barrier::UAV(cmdList, target);
            Barrier::UAV(cmdList, targetDepth);
            Barrier::Transition(cmdList, target, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            Barrier::Transition(cmdList, targetDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
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
