#include "pch.h"
#include "CloudNoiseBaker.h"

#include "Graphics/Cloud/Shader/CloudBindings.h"
#include "Graphics/Cloud/Shader/CloudPipelines.h"
#include "Graphics/Cloud/Render/CloudRenderContext.h"
#include "Graphics/Cloud/Render/CloudStageScope.h"
#include "Graphics/Cloud/Resource/CloudResources.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>

namespace CoreEngine
{
    void CloudNoiseBaker::BakeIfNeeded(const CloudRenderContext& ctx)
    {
        if (paintDirty_) {
            UploadPaintTexture(ctx);
            paintDirty_ = false;
        }

        if (!dirty_) {
            return;
        }

        ID3D12GraphicsCommandList* cmdList = ctx.cmdList;

        // 各ノイズ CS: UAV へ書き込み → 描画/レイマーチが読めるよう SRV 状態へ遷移。
        // ノイズシェーダーは定数バッファ不要（純手続き生成）。gOutput UAV のみバインドする。
        auto dispatchNoise = [&](CloudPass passId, const char* stageName, CloudGpuTexture& tex,
                                 UINT gx, UINT gy, UINT gz)
        {
            CloudStageScope stage(ctx, stageName);

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

        // ミップ連鎖: 1 段下をボックス平均で作る。生成元の段だけを SRV へ、
        // 生成先の段だけを UAV へ遷移させるので、遷移はサブリソース単位で張る。
        auto buildMips = [&](CloudGpuTexture& tex, const char* stageName, uint32_t size)
        {
            const uint32_t mipLevels = tex.MipLevels();
            if (mipLevels <= 1) {
                return;
            }

            CloudStageScope stage(ctx, stageName);
            const CloudComputePass& pass = (*ctx.pipelines)[CloudPass::NoiseMip3D];

            for (uint32_t mip = 1; mip < mipLevels; ++mip) {
                Barrier::Transition(cmdList, tex,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, mip - 1);
                Barrier::Transition(cmdList, tex, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, mip);

                ShaderBinder binder = pass.Begin(cmdList);
                binder.Set(pass.bindings[CloudNoiseMipBind::gSource], tex.mipSrvs[mip - 1].gpuHandle);
                binder.Set(pass.bindings[CloudNoiseMipBind::gOutput], tex.mipUavs[mip].gpuHandle);
                binder.ValidateBeforeDraw(pass.bindings);

                const uint32_t extent = std::max(size >> mip, 1u);
                const UINT groups = (extent + 3u) / 4u;   // numthreads(4,4,4)
                cmdList->Dispatch(groups, groups, groups);
            }

            Barrier::UAV(cmdList, tex);
            Barrier::Transition(cmdList, tex,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        };

        const UINT baseGroups = CloudResources::kBaseShapeNoiseSize / 4;   // numthreads(4,4,4)
        dispatchNoise(CloudPass::BaseShapeNoise, "Cloud Noise BaseShape", ctx.resources->baseShapeNoise,
            baseGroups, baseGroups, baseGroups);
        buildMips(ctx.resources->baseShapeNoise, "Cloud Noise BaseShape Mips",
            CloudResources::kBaseShapeNoiseSize);

        const UINT detailGroups = CloudResources::kDetailNoiseSize / 4;    // numthreads(4,4,4)
        dispatchNoise(CloudPass::DetailNoise, "Cloud Noise Detail", ctx.resources->detailNoise,
            detailGroups, detailGroups, detailGroups);
        buildMips(ctx.resources->detailNoise, "Cloud Noise Detail Mips",
            CloudResources::kDetailNoiseSize);

        const UINT weatherGroups = CloudResources::kWeatherMapSize / 8;    // numthreads(8,8,1)
        dispatchNoise(CloudPass::WeatherMap, "Cloud Noise Weather", ctx.resources->weatherMap,
            weatherGroups, weatherGroups, 1);

        dirty_ = false;
        generated_ = true;

        Logger::GetInstance().Infof(LogCategory::Graphics,
            "CloudNoiseBaker: ノイズテクスチャ生成完了 (BaseShape {0}^3 / Detail {1}^3 / Weather {2}^2)",
            CloudResources::kBaseShapeNoiseSize,
            CloudResources::kDetailNoiseSize,
            CloudResources::kWeatherMapSize);
    }

    void CloudNoiseBaker::UploadPaintTexture(const CloudRenderContext& ctx)
    {
        CloudResources& res = *ctx.resources;
        if (!res.weatherPaintUpload || !res.weatherPaint) {
            return;
        }

        ID3D12GraphicsCommandList* cmdList = ctx.cmdList;
        CloudStageScope stage(ctx, "Cloud Paint Upload");

        Barrier::Transition(cmdList, res.weatherPaint, D3D12_RESOURCE_STATE_COPY_DEST);

        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = res.weatherPaintUpload.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Offset = 0;
        src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        src.PlacedFootprint.Footprint.Width = CloudResources::kPaintSize;
        src.PlacedFootprint.Footprint.Height = CloudResources::kPaintSize;
        src.PlacedFootprint.Footprint.Depth = 1;
        src.PlacedFootprint.Footprint.RowPitch = CloudResources::kPaintSize * 4;

        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = res.weatherPaint.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = 0;

        cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

        Barrier::Transition(cmdList, res.weatherPaint,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
}
