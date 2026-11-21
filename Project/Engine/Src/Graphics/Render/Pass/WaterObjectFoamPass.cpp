#include "pch.h"
#include "WaterObjectFoamPass.h"

#include "Graphics/Render/RenderGraph.h"
#include "Graphics/Water/Foam/WaterObjectFoam.h"
#include "Graphics/Water/WaterSurfaceData.h"

namespace CoreEngine
{
    void WaterObjectFoamPass::DeclareResources(RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        // 源を FFT の波の水平の変位で動かした位置で比べ、陸を海の範囲マップで決める
        builder.Read(FrameBlackboard::FFTOceanDisplacement, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Read(FrameBlackboard::WaterSeaMap,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    void WaterObjectFoamPass::Execute(const RenderContext& context)
    {
        WaterObjectFoam* const objectFoam = context.waterObjectFoam;
        if (!objectFoam || !objectFoam->IsInitialized() || !context.cmdList) {
            return;
        }
        // 格子はゲームのカメラで置き場所を決めてビューに依らないので、フレームに 1 回だけ進める
        const WaterFFTOceanInput* const fftOcean = context.waterSurfaceState ? &context.waterSurfaceState->fftOcean : nullptr;
        objectFoam->Dispatch(context.cmdList, context.frameNumber, fftOcean);
    }
}
