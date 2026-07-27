#include "pch.h"
#include "Graphics/Water/Simulation/FFTOceanSurfaceSimulator.h"

namespace CoreEngine
{
    void FFTOceanSurfaceSimulator::AdvanceSimulation(float deltaTime)
    {
        // FFT Ocean の GPU dispatch と同じ時間基準で surface snapshot を管理する
        elapsedTime_ += deltaTime;
    }

    void FFTOceanSurfaceSimulator::CaptureSurface(
        const WaterSurfaceSimulationInput& input,
        WaterSurfaceData& surfaceData) const
    {
        surfaceData = {};

        // FFT 経路では Gerstner 波配列に依存せず、時間と高さだけを surface として保持する
        surfaceData.waterHeight = input.waterHeight;
        surfaceData.activeWaveCount = 0;
        surfaceData.time = elapsedTime_;
        surfaceData.simulationType = kWaterSurfaceModelTypeFFTOcean;
    }
}
