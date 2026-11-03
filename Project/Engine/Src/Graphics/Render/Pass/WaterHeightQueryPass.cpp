#include "pch.h"
#include "WaterHeightQueryPass.h"

#include "Graphics/Water/FFT/FFTOceanManager.h"
#include "Graphics/Water/Query/WaterHeightQueryManager.h"

namespace CoreEngine
{
    void WaterHeightQueryPass::Execute(const RenderContext& context)
    {
        WaterHeightQueryManager* const manager = context.waterHeightQueryManager;
        if (!manager || !manager->IsInitialized() || !context.cmdList) {
            return;
        }

        // ビューごとには評価せず、フレームに 1 回だけ評価する
        if (lastDispatchFrame_ == context.frameNumber) {
            return;
        }
        lastDispatchFrame_ = context.frameNumber;

        // Gerstner の波面のときも、変位テクスチャのスロットには FFT の変位を差す
        D3D12_GPU_DESCRIPTOR_HANDLE fallbackDisplacementSRV{};
        if (context.fftOceanManager && context.fftOceanManager->IsInitialized()) {
            fallbackDisplacementSRV = context.fftOceanManager->GetDisplacementSRVHandle();
        }

        manager->Dispatch(
            context.cmdList, context.waterSurfaceState, context.otherWaterSurfaceStates, fallbackDisplacementSRV);
    }
}
