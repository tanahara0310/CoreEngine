#pragma once

#include <cstdint>

namespace CoreEngine
{
    /// @brief 水面の波のモデル（値は RTWaterSurfaceCommon.hlsli の kWaterSurfaceModelType* と同じ）
    enum class WaterWaveModelType : uint32_t {
        Gerstner = 0,
        FFTOcean = 1,
    };

    /// @brief 波のモデルの数
    inline constexpr uint32_t kWaterWaveModelTypeCount = 2;
}
