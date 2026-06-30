#pragma once

#include <cstdint>

/// @brief 水面デバッグ可視化モード
enum class WaterDebugViewMode : uint32_t {
    None = 0,
    RawDepth = 1,
    LinearDepth = 2,
    DepthDelta = 3,
    ScreenUV = 4,
    SceneColor = 5,
    Reflection = 6,
    Fresnel = 7,
    RTRefraction = 8,
    RTRefractionReason = 9,
    RTRefractionVsScene = 10,
    Transmission = 11,
    Absorption = 12,
    Reflectance = 13,
    WaterComposite = 14,
    RTRefractionSuccessMask = 15,
    FFTOceanJacobian = 16,
};
