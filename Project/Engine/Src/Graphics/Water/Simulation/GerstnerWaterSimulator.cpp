#include "pch.h"
#include "Graphics/Water/Simulation/GerstnerWaterSimulator.h"

#include <algorithm>

namespace CoreEngine
{
    namespace
    {
        /// @brief Gerstner 用 WaveParams を DXR 用 WaterWaveParam へ変換する
        WaterWaveParam ConvertToWaterWaveParam(const WaveParams& wave)
        {
            WaterWaveParam result{};
            result.direction[0] = wave.direction.x;
            result.direction[1] = wave.direction.y;
            result.amplitude = wave.amplitude;
            result.wavelength = wave.wavelength;
            result.speed = wave.speed;
            result.steepness = wave.steepness;
            result.phaseOffset = wave.phaseOffset;
            result.padding = wave.padding;
            return result;
        }
    }

    void GerstnerWaterSimulator::AdvanceSimulation(float deltaTime)
    {
        // Gerstner 波の位相計算に使用する経過時間を加算する
        elapsedTime_ += deltaTime;
    }

    void GerstnerWaterSimulator::CaptureSurface(
        const WaterSurfaceSimulationInput& input,
        WaterSurfaceSnapshot& snapshot,
        WaterSurfaceData& surfaceData) const
    {
        snapshot = {};
        surfaceData = {};

        if (!input.gerstnerConstants) {
            return;
        }

        // 共通 snapshot と DXR 用 surface data の両方へ同じ水面基底情報を反映する
        const WaterConstants& waterConstants = *input.gerstnerConstants;
        snapshot.waterHeight = input.waterHeight;
        snapshot.activeWaveCount = (std::min)(waterConstants.activeWaveCount, kMaxWaterSurfaceWaveCount);
        snapshot.time = elapsedTime_;

        surfaceData.waterHeight = snapshot.waterHeight;
        surfaceData.activeWaveCount = snapshot.activeWaveCount;
        surfaceData.time = snapshot.time;

        for (uint32_t waveIndex = 0; waveIndex < snapshot.activeWaveCount; ++waveIndex) {
            snapshot.waves[waveIndex] = waterConstants.waves[waveIndex];
            surfaceData.waves[waveIndex] = ConvertToWaterWaveParam(waterConstants.waves[waveIndex]);
        }
    }
}
