#include "pch.h"
#include "WaterHeightQueryPass.h"

#include "Graphics/Water/FFTOceanManager.h"
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
        WaterRayTracingPassBase::FFTOceanInput fftOceanInput{};
        if (context.waterSurfaceState) {
            fftOceanInput = WaterRayTracingPassBase::MakeFFTOceanInput(
                context.fftOceanManager, context.fftOceanSimulationTime, context.waterSurfaceState->simulationType);
        }
        if (fftOceanInput.displacementSRV.ptr == 0 && context.fftOceanManager && context.fftOceanManager->IsInitialized()) {
            fftOceanInput.displacementSRV = context.fftOceanManager->GetDisplacementSRVHandle();
        }

        manager->Dispatch(context.cmdList, context.waterSurfaceState, fftOceanInput);
    }
}
