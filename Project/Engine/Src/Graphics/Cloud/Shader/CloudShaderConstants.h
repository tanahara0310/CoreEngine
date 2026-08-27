#pragma once

#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"
#include "Math/MathCore.h"

#include <cstdint>

namespace CoreEngine
{

    /// @brief 雲シェーダーへ渡す定数バッファレイアウト
    /// @details HLSL 側 CloudCommon.hlsli の CloudConstants と一致させること（304 バイト）。
    ///          距離はメートル基準。sunDirection は「光の進行方向」（大気散乱と同じ規約）。
    struct VolumetricCloudShaderConstants {
        Matrix4x4 invViewProj;                                              // 0
        Vector3 cameraWorldPos;      float timeSec;                         // 64
        Vector3 sunDirection;        float sunIntensity;                    // 80
        Vector3 sunColor;            float planetRadiusM;                   // 96
        float layerBottomAltitudeM;  float layerThicknessM;
        float groundLevelY;          float globalCoverage;                  // 112
        float baseNoiseScaleM;       float detailNoiseScaleM;
        float detailErosionStrength; float densityScale;                   // 128
        float windDirX;              float windDirZ;
        float windSpeedMPerS;        float weatherMapScaleM;               // 144
        float phaseG0;               float phaseG1;
        float phaseBlend;            float ambientIntensity;                // 160
        float beerPowderStrength;    float lightMarchStepM;
        float earlyExitTransmittance; float maxMarchDistanceM;             // 176
        uint32_t maxSteps;           uint32_t outputWidth;
        uint32_t outputHeight;       uint32_t pad0;                         // 192
        float sunLightScale;         float msAttenuation;
        float msContribution;        float msEccentricity;                  // 208
        // ===== 月（第2大気ライト。夜の雲の直接照明） =====
        Vector3 moonDirection;       float moonIntensity;                   // 224
        Vector3 moonColor;           float hasMoon;                         // 240
        // ===== 見た目のチューニング値 =====
        float baseNoiseVerticalScale; float heightSkewM;
        float detailFadeDistanceM;   float farFadeWidthM;                   // 256
        float hazeDistanceM;         float maxSunOpticalDepth;
        float ambientCosZenith;      float ambientBottomOcclusion;          // 272
        float ambientChroma;         float pad1;
        float pad2;                  float pad3;                            // 288 (= 304)
    };
    static_assert(sizeof(VolumetricCloudShaderConstants) == 304,
        "VolumetricCloudShaderConstants は HLSL 側 CloudConstants の 304 バイトレイアウトと一致させること");

    static constexpr Cb::Field kVolumetricCloudShaderConstantsFields[] = {
        CB_FIELD(VolumetricCloudShaderConstants, invViewProj),
        CB_FIELD(VolumetricCloudShaderConstants, cameraWorldPos),
        CB_FIELD(VolumetricCloudShaderConstants, timeSec), CB_FIELD(VolumetricCloudShaderConstants, sunDirection),
        CB_FIELD(VolumetricCloudShaderConstants, sunIntensity), CB_FIELD(VolumetricCloudShaderConstants, sunColor),
        CB_FIELD(VolumetricCloudShaderConstants, planetRadiusM),
        CB_FIELD(VolumetricCloudShaderConstants, layerBottomAltitudeM),
        CB_FIELD(VolumetricCloudShaderConstants, layerThicknessM),
        CB_FIELD(VolumetricCloudShaderConstants, groundLevelY),
        CB_FIELD(VolumetricCloudShaderConstants, globalCoverage),
        CB_FIELD(VolumetricCloudShaderConstants, baseNoiseScaleM),
        CB_FIELD(VolumetricCloudShaderConstants, detailNoiseScaleM),
        CB_FIELD(VolumetricCloudShaderConstants, detailErosionStrength),
        CB_FIELD(VolumetricCloudShaderConstants, densityScale), CB_FIELD(VolumetricCloudShaderConstants, windDirX),
        CB_FIELD(VolumetricCloudShaderConstants, windDirZ),
        CB_FIELD(VolumetricCloudShaderConstants, windSpeedMPerS),
        CB_FIELD(VolumetricCloudShaderConstants, weatherMapScaleM),
        CB_FIELD(VolumetricCloudShaderConstants, phaseG0), CB_FIELD(VolumetricCloudShaderConstants, phaseG1),
        CB_FIELD(VolumetricCloudShaderConstants, phaseBlend),
        CB_FIELD(VolumetricCloudShaderConstants, ambientIntensity),
        CB_FIELD(VolumetricCloudShaderConstants, beerPowderStrength),
        CB_FIELD(VolumetricCloudShaderConstants, lightMarchStepM),
        CB_FIELD(VolumetricCloudShaderConstants, earlyExitTransmittance),
        CB_FIELD(VolumetricCloudShaderConstants, maxMarchDistanceM),
        CB_FIELD(VolumetricCloudShaderConstants, maxSteps), CB_FIELD(VolumetricCloudShaderConstants, outputWidth),
        CB_FIELD(VolumetricCloudShaderConstants, outputHeight),
        CB_FIELD(VolumetricCloudShaderConstants, pad0),
        CB_FIELD(VolumetricCloudShaderConstants, sunLightScale),
        CB_FIELD(VolumetricCloudShaderConstants, msAttenuation),
        CB_FIELD(VolumetricCloudShaderConstants, msContribution),
        CB_FIELD(VolumetricCloudShaderConstants, msEccentricity),
        CB_FIELD(VolumetricCloudShaderConstants, moonDirection),
        CB_FIELD(VolumetricCloudShaderConstants, moonIntensity),
        CB_FIELD(VolumetricCloudShaderConstants, moonColor), CB_FIELD(VolumetricCloudShaderConstants, hasMoon),
        CB_FIELD(VolumetricCloudShaderConstants, baseNoiseVerticalScale),
        CB_FIELD(VolumetricCloudShaderConstants, heightSkewM),
        CB_FIELD(VolumetricCloudShaderConstants, detailFadeDistanceM),
        CB_FIELD(VolumetricCloudShaderConstants, farFadeWidthM),
        CB_FIELD(VolumetricCloudShaderConstants, hazeDistanceM),
        CB_FIELD(VolumetricCloudShaderConstants, maxSunOpticalDepth),
        CB_FIELD(VolumetricCloudShaderConstants, ambientCosZenith),
        CB_FIELD(VolumetricCloudShaderConstants, ambientBottomOcclusion),
        CB_FIELD(VolumetricCloudShaderConstants, ambientChroma),
        CB_FIELD(VolumetricCloudShaderConstants, pad1), CB_FIELD(VolumetricCloudShaderConstants, pad2),
        CB_FIELD(VolumetricCloudShaderConstants, pad3),
    };
    CB_VERIFY_LAYOUT(VolumetricCloudShaderConstants, kVolumetricCloudShaderConstantsFields);
    CB_BIND_HLSL(VolumetricCloudShaderConstants, kVolumetricCloudShaderConstantsFields, "gCloud");

    /// @brief ゴッドレイシェーダーへ渡す定数バッファレイアウト
    /// @details HLSL 側 GodRayCommon.hlsli の GodRayConstants と一致させること（128 バイト）。
    ///          太陽方向・散乱係数などは gAtmosphere / gCloud 側 CB から取るため持たない。
    struct GodRayShaderConstants {
        Matrix4x4 invViewProj;                                      // 0
        Vector3 cameraWorldPos;      float maxDistanceM;            // 64
        float shadowRegionCenterX;   float shadowRegionCenterZ;
        float shadowRegionSizeM;     float shadowAnchorWorldY;      // 80
        float intensity;             float mieBoost;
        float groundLevelY;          float edgeFadeStart;           // 96
        uint32_t stepCount;          uint32_t outputWidth;
        uint32_t outputHeight;       uint32_t pad0;                 // 112 (= 128)
    };
    static_assert(sizeof(GodRayShaderConstants) == 128,
        "GodRayShaderConstants は HLSL 側 GodRayConstants の 128 バイトレイアウトと一致させること");

    static constexpr Cb::Field kGodRayShaderConstantsFields[] = {
        CB_FIELD(GodRayShaderConstants, invViewProj), CB_FIELD(GodRayShaderConstants, cameraWorldPos),
        CB_FIELD(GodRayShaderConstants, maxDistanceM), CB_FIELD(GodRayShaderConstants, shadowRegionCenterX),
        CB_FIELD(GodRayShaderConstants, shadowRegionCenterZ), CB_FIELD(GodRayShaderConstants, shadowRegionSizeM),
        CB_FIELD(GodRayShaderConstants, shadowAnchorWorldY), CB_FIELD(GodRayShaderConstants, intensity),
        CB_FIELD(GodRayShaderConstants, mieBoost), CB_FIELD(GodRayShaderConstants, groundLevelY),
        CB_FIELD(GodRayShaderConstants, edgeFadeStart), CB_FIELD(GodRayShaderConstants, stepCount),
        CB_FIELD(GodRayShaderConstants, outputWidth), CB_FIELD(GodRayShaderConstants, outputHeight),
        CB_FIELD(GodRayShaderConstants, pad0),
    };
    CB_VERIFY_LAYOUT(GodRayShaderConstants, kGodRayShaderConstantsFields);
    CB_BIND_HLSL(GodRayShaderConstants, kGodRayShaderConstantsFields, "gGodRay");
}
