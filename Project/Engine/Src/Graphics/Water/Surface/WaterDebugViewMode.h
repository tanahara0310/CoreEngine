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
    Transmittance = 12,
    Reflectance = 13,
    WaterComposite = 14,
    RTRefractionSuccessMask = 15,
    FFTOceanJacobian = 16,
    PlanarReflectionRaw = 17,
    SkyEnvCloudColor = 18,
    CloudOverlayWeight = 19,
    // まだら診断: フレネル混合を強制して各端点を単独表示する
    CompositeTransmissionOnly = 20, // reflectanceWeight=0 の最終合成（透過のみ）
    CompositeReflectionOnly = 21,   // reflectanceWeight=1 の最終合成（反射のみ）
    ReflectionMinusTransmission = 22, // |反射 - 透過| ×3（斑を生む輝度差の分布）
    FFTOceanFoam = 23, // 泡マスク（グレースケール。foamBias/foamGain の較正用）
};
