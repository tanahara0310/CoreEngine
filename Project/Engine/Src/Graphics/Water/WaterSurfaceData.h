#pragma once

#include "Graphics/Water/Surface/WaterSurfaceTypes.h"

namespace CoreEngine
{
    static constexpr uint32_t kMaxWaterSurfaceWaveCount = kMaxWaterWaveCount;
    static constexpr uint32_t kWaterSurfaceModelTypeGerstner = 0;
    static constexpr uint32_t kWaterSurfaceModelTypeFFTOcean = 1;

    struct WaterWaveParam {
        float direction[2] = { 1.0f, 0.0f };
        float amplitude = 0.0f;
        float wavelength = 1.0f;
        float speed = 0.0f;
        float steepness = 0.0f;
        float phaseOffset = 0.0f;
        float padding = 0.0f;
    };

    struct WaterSurfaceSnapshot {
        float waterHeight = 0.0f;
        uint32_t activeWaveCount = 0;
        float time = 0.0f;
        uint32_t simulationType = kWaterSurfaceModelTypeGerstner;
        WaveParams waves[kMaxWaterSurfaceWaveCount]{};
    };

    struct WaterSurfaceData {
        float waterHeight = 0.0f;
        uint32_t activeWaveCount = 0;
        float time = 0.0f;
        uint32_t simulationType = kWaterSurfaceModelTypeGerstner;
        WaterWaveParam waves[kMaxWaterSurfaceWaveCount]{};
    };

    struct WaterOpticalProperties {
        float refractiveIndex = 1.333f;
        float absorptionCoeff = 0.3f;
        float scatteringCoeff = 0.0f;
        float anisotropy = 0.0f;
    };

}
