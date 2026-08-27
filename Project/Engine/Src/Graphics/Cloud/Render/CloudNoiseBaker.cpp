#include "pch.h"
#include "CloudNoiseBaker.h"

#include "Graphics/Cloud/Shader/CloudBindings.h"
#include "Graphics/Cloud/Shader/CloudPipelines.h"
#include "Graphics/Cloud/Render/CloudRenderContext.h"
#include "Graphics/Cloud/Resource/CloudResources.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    void CloudNoiseBaker::BakeIfNeeded(const CloudRenderContext& ctx)
    {
        if (!dirty_) {
            return;
        }

        ID3D12GraphicsCommandList* cmdList = ctx.cmdList;

        // 各ノイズ CS: UAV へ書き込み → 描画/レイマーチが読めるよう SRV 状態へ遷移。
        // ノイズシェーダーは定数バッファ不要（純手続き生成）。gOutput UAV のみバインドする。
        auto dispatchNoise = [&](CloudPass passId, CloudGpuTexture& tex,
                                 UINT gx, UINT gy, UINT gz)
        {
            Barrier::Transition(cmdList, tex, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            const CloudComputePass& pass = (*ctx.pipelines)[passId];
            ShaderBinder binder = pass.Begin(cmdList);
            binder.Set(pass.bindings[CloudNoiseBind::gOutput], tex.uav.gpuHandle);
            binder.ValidateBeforeDraw(pass.bindings);

            cmdList->Dispatch(gx, gy, gz);

            Barrier::UAV(cmdList, tex);
            Barrier::Transition(cmdList, tex,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        };

        const UINT baseGroups = CloudResources::kBaseShapeNoiseSize / 4;   // numthreads(4,4,4)
        dispatchNoise(CloudPass::BaseShapeNoise, ctx.resources->baseShapeNoise,
            baseGroups, baseGroups, baseGroups);

        const UINT detailGroups = CloudResources::kDetailNoiseSize / 4;    // numthreads(4,4,4)
        dispatchNoise(CloudPass::DetailNoise, ctx.resources->detailNoise,
            detailGroups, detailGroups, detailGroups);

        const UINT weatherGroups = CloudResources::kWeatherMapSize / 8;    // numthreads(8,8,1)
        dispatchNoise(CloudPass::WeatherMap, ctx.resources->weatherMap,
            weatherGroups, weatherGroups, 1);

        dirty_ = false;
        generated_ = true;

        Logger::GetInstance().Infof(LogCategory::Graphics,
            "CloudNoiseBaker: ノイズテクスチャ生成完了 (BaseShape {0}^3 / Detail {1}^3 / Weather {2}^2)",
            CloudResources::kBaseShapeNoiseSize,
            CloudResources::kDetailNoiseSize,
            CloudResources::kWeatherMapSize);
    }
}
