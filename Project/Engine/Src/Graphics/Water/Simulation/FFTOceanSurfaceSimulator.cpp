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
        WaterSurfaceSnapshot& snapshot,
        WaterSurfaceData& surfaceData) const
    {
        snapshot = {};
        surfaceData = {};

        // FFT 経路では Gerstner 波配列に依存せず、時間と高さだけを共通 surface として保持する
        snapshot.waterHeight = input.waterHeight;
        snapshot.activeWaveCount = 0;
        snapshot.time = elapsedTime_;
        snapshot.simulationType = kWaterSurfaceModelTypeFFTOcean;

        surfaceData.waterHeight = snapshot.waterHeight;
        surfaceData.activeWaveCount = 0;
        surfaceData.time = snapshot.time;
        surfaceData.simulationType = snapshot.simulationType;
    }
}
