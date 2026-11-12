#include "pch.h"
#include "GerstnerWaveModel.h"

#include <algorithm>
#include <cmath>

#include "Graphics/Water/Surface/WaterSurfaceComponent.h"
#include "Graphics/Water/WaterSurfaceData.h"

namespace CoreEngine
{
    void GerstnerWaveModel::Advance(float deltaSeconds, bool drawn)
    {
        if (drawn) {
            time_ += deltaSeconds;
        }
    }

    void GerstnerWaveModel::Capture(
        [[maybe_unused]] RenderDomainContext& domain,
        const WaterSurfaceComponent& waterPlane,
        WaterSurfaceData& snapshot) const
    {
        snapshot = {};

        const WaterConstants& waterConstants = waterPlane.GetWaterConstants();
        snapshot.waterHeight = waterPlane.GetWaterLevel();
        snapshot.activeWaveCount = (std::min)(waterConstants.activeWaveCount, kMaxWaterSurfaceWaveCount);
        snapshot.time = time_;
        snapshot.simulationType = static_cast<uint32_t>(GetType());
        for (uint32_t waveIndex = 0; waveIndex < snapshot.activeWaveCount; ++waveIndex) {
            snapshot.waves[waveIndex] = waterConstants.waves[waveIndex];
        }
    }

    float GerstnerWaveModel::ComputeSignificantWaveHeight(
        [[maybe_unused]] RenderDomainContext& domain, const WaterSurfaceData& snapshot) const
    {
        // 正弦の波の分散は振幅の二乗の半分。有義波高は標準偏差の 4 倍
        float sumSquares = 0.0f;
        for (uint32_t waveIndex = 0; waveIndex < snapshot.activeWaveCount; ++waveIndex) {
            const float amplitude = snapshot.waves[waveIndex].amplitude;
            sumSquares += amplitude * amplitude;
        }
        return 4.0f * std::sqrt(0.5f * sumSquares);
    }

    float GerstnerWaveModel::ComputeMaxCrestHeight(
        [[maybe_unused]] RenderDomainContext& domain, const WaterSurfaceData& snapshot) const
    {
        // 全部の波の山が重なった高さ
        float sumAmplitudes = 0.0f;
        for (uint32_t waveIndex = 0; waveIndex < snapshot.activeWaveCount; ++waveIndex) {
            sumAmplitudes += std::abs(snapshot.waves[waveIndex].amplitude);
        }
        return sumAmplitudes;
    }
}
