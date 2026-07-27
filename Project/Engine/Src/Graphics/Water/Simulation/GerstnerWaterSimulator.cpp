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

        for (uint32_t waveIndex = 0; waveIndex < surfaceData.activeWaveCount; ++waveIndex) {
            surfaceData.waves[waveIndex] = ConvertToWaterWaveParam(waterConstants.waves[waveIndex]);
        }
    }
}
