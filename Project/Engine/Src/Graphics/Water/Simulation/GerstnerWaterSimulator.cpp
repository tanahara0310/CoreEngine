#include "pch.h"
#include "Graphics/Water/Simulation/GerstnerWaterSimulator.h"

#include <algorithm>

namespace CoreEngine
{
    void GerstnerWaterSimulator::AdvanceSimulation(float deltaTime)
    {
        // Gerstner 波の位相計算に使用する経過時間を加算する
        elapsedTime_ += deltaTime;
    }

    void GerstnerWaterSimulator::CaptureSurface(
        const WaterSurfaceSimulationInput& input,
        WaterSurfaceData& surfaceData) const
    {
        surfaceData = {};

        if (!input.gerstnerConstants) {
            return;
        }

        const WaterConstants& waterConstants = *input.gerstnerConstants;
        surfaceData.waterHeight = input.waterHeight;
        surfaceData.activeWaveCount = (std::min)(waterConstants.activeWaveCount, kMaxWaterSurfaceWaveCount);
        surfaceData.time = elapsedTime_;
        surfaceData.simulationType = kWaterSurfaceModelTypeGerstner;

        // 型統合（WaterWaveParam = WaveParams）によりフィールド単位の変換は不要
        for (uint32_t waveIndex = 0; waveIndex < surfaceData.activeWaveCount; ++waveIndex) {
            surfaceData.waves[waveIndex] = waterConstants.waves[waveIndex];
        }
    }
}
