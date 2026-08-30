#pragma once

#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"
#include "Math/MathCore.h"

#include <cstdint>

namespace CoreEngine
{

    /// @brief 雲シェーダーへ渡す定数バッファレイアウト
    /// @details HLSL 側 CloudCommon.hlsli の CloudConstants と一致させること（416 バイト）。
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
        float dropletDiameterUm;     float maxPhase;
        float lightMarchConeSpread;  float ambientIntensity;                // 160
        float beerPowderStrength;    float lightMarchCoverage;
        float earlyExitTransmittance; float maxMarchDistanceM;             // 176
        uint32_t maxSteps;           uint32_t outputWidth;
        uint32_t outputHeight;       uint32_t frameIndex;                   // 192
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
        float ambientChroma;         float ambientGroundStrength;
        float upsampleDepthTolerance; float cloudStreetStretch;             // 288
        // ===== 時間再投影 =====
        Matrix4x4 prevViewProj;                                             // 304
        float reprojectEnabled;      float reprojectBlendMin;
        float reprojectTolerance;    float cloudTopVariation;               // 368
        // ===== 巻雲シェル =====
        float cirrusAltitudeM;       float cirrusCoverage;
        float cirrusDensity;         float cirrusScaleM;                    // 384
        float cirrusStretch;         float cirrusWindScale;
        float noiseLodBias;          float paintRegionCenterX;              // 400
        // ===== 配置ペイント（ワールド固定領域） =====
        float paintRegionCenterZ;    float paintRegionSizeM;
        float paintEdgeFade;         float pad7;                            // 416 (= 432)
    };
    static_assert(sizeof(VolumetricCloudShaderConstants) == 432,
        "VolumetricCloudShaderConstants は HLSL 側 CloudConstants の 432 バイトレイアウトと一致させること");

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
        CB_FIELD(VolumetricCloudShaderConstants, dropletDiameterUm),
        CB_FIELD(VolumetricCloudShaderConstants, maxPhase),
        CB_FIELD(VolumetricCloudShaderConstants, lightMarchConeSpread),
        CB_FIELD(VolumetricCloudShaderConstants, ambientIntensity),
        CB_FIELD(VolumetricCloudShaderConstants, beerPowderStrength),
        CB_FIELD(VolumetricCloudShaderConstants, lightMarchCoverage),
        CB_FIELD(VolumetricCloudShaderConstants, earlyExitTransmittance),
        CB_FIELD(VolumetricCloudShaderConstants, maxMarchDistanceM),
        CB_FIELD(VolumetricCloudShaderConstants, maxSteps), CB_FIELD(VolumetricCloudShaderConstants, outputWidth),
        CB_FIELD(VolumetricCloudShaderConstants, outputHeight),
        CB_FIELD(VolumetricCloudShaderConstants, frameIndex),
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
        CB_FIELD(VolumetricCloudShaderConstants, ambientGroundStrength),
        CB_FIELD(VolumetricCloudShaderConstants, upsampleDepthTolerance),
        CB_FIELD(VolumetricCloudShaderConstants, cloudStreetStretch),
        CB_FIELD(VolumetricCloudShaderConstants, prevViewProj),
        CB_FIELD(VolumetricCloudShaderConstants, reprojectEnabled),
        CB_FIELD(VolumetricCloudShaderConstants, reprojectBlendMin),
        CB_FIELD(VolumetricCloudShaderConstants, reprojectTolerance),
        CB_FIELD(VolumetricCloudShaderConstants, cloudTopVariation),
        CB_FIELD(VolumetricCloudShaderConstants, cirrusAltitudeM),
        CB_FIELD(VolumetricCloudShaderConstants, cirrusCoverage),
        CB_FIELD(VolumetricCloudShaderConstants, cirrusDensity),
        CB_FIELD(VolumetricCloudShaderConstants, cirrusScaleM),
        CB_FIELD(VolumetricCloudShaderConstants, cirrusStretch),
        CB_FIELD(VolumetricCloudShaderConstants, cirrusWindScale),
        CB_FIELD(VolumetricCloudShaderConstants, noiseLodBias),
        CB_FIELD(VolumetricCloudShaderConstants, paintRegionCenterX),
        CB_FIELD(VolumetricCloudShaderConstants, paintRegionCenterZ),
        CB_FIELD(VolumetricCloudShaderConstants, paintRegionSizeM),
        CB_FIELD(VolumetricCloudShaderConstants, paintEdgeFade),
        CB_FIELD(VolumetricCloudShaderConstants, pad7),
    };
    CB_VERIFY_LAYOUT(VolumetricCloudShaderConstants, kVolumetricCloudShaderConstantsFields);
    CB_BIND_HLSL(VolumetricCloudShaderConstants, kVolumetricCloudShaderConstantsFields, "gCloud");

    /// @brief 雲シャドウマップのパラメータ化
    /// @details HLSL 側 CloudShadowCommon.hlsli の CloudShadowConstants と一致させること（32 バイト）。
    ///          生成 CS・ゴッドレイ・Deferred ライティングの 3 者が同じ値を読む。
    struct CloudShadowShaderConstants {
        float regionCenterX;         float regionCenterZ;
        float regionSizeM;           float anchorWorldY;            // 0
        float edgeFadeStart;         float sceneStrength;
        float pad0;                  float pad1;                    // 16 (= 32)
    };
    static_assert(sizeof(CloudShadowShaderConstants) == 32,
        "CloudShadowShaderConstants は HLSL 側 CloudShadowConstants の 32 バイトレイアウトと一致させること");

    static constexpr Cb::Field kCloudShadowShaderConstantsFields[] = {
        CB_FIELD(CloudShadowShaderConstants, regionCenterX), CB_FIELD(CloudShadowShaderConstants, regionCenterZ),
        CB_FIELD(CloudShadowShaderConstants, regionSizeM), CB_FIELD(CloudShadowShaderConstants, anchorWorldY),
        CB_FIELD(CloudShadowShaderConstants, edgeFadeStart), CB_FIELD(CloudShadowShaderConstants, sceneStrength),
        CB_FIELD(CloudShadowShaderConstants, pad0), CB_FIELD(CloudShadowShaderConstants, pad1),
    };
    CB_VERIFY_LAYOUT(CloudShadowShaderConstants, kCloudShadowShaderConstantsFields);
    CB_BIND_HLSL(CloudShadowShaderConstants, kCloudShadowShaderConstantsFields, "gCloudShadow");

    /// @brief ゴッドレイシェーダーへ渡す定数バッファレイアウト
    /// @details HLSL 側 GodRayCommon.hlsli の GodRayConstants と一致させること（112 バイト）。
    ///          太陽方向・散乱係数は gAtmosphere / gCloud、シャドウ範囲は gCloudShadow から取る。
    struct GodRayShaderConstants {
        Matrix4x4 invViewProj;                                      // 0
        Vector3 cameraWorldPos;      float maxDistanceM;            // 64
        float intensity;             float mieBoost;
        float groundLevelY;          float pad1;                    // 80
        uint32_t stepCount;          uint32_t outputWidth;
        uint32_t outputHeight;       uint32_t pad0;                 // 96 (= 112)
    };
    static_assert(sizeof(GodRayShaderConstants) == 112,
        "GodRayShaderConstants は HLSL 側 GodRayConstants の 112 バイトレイアウトと一致させること");

    static constexpr Cb::Field kGodRayShaderConstantsFields[] = {
        CB_FIELD(GodRayShaderConstants, invViewProj), CB_FIELD(GodRayShaderConstants, cameraWorldPos),
        CB_FIELD(GodRayShaderConstants, maxDistanceM), CB_FIELD(GodRayShaderConstants, intensity),
        CB_FIELD(GodRayShaderConstants, mieBoost), CB_FIELD(GodRayShaderConstants, groundLevelY),
        CB_FIELD(GodRayShaderConstants, pad1), CB_FIELD(GodRayShaderConstants, stepCount),
        CB_FIELD(GodRayShaderConstants, outputWidth), CB_FIELD(GodRayShaderConstants, outputHeight),
        CB_FIELD(GodRayShaderConstants, pad0),
    };
    CB_VERIFY_LAYOUT(GodRayShaderConstants, kGodRayShaderConstantsFields);
    CB_BIND_HLSL(GodRayShaderConstants, kGodRayShaderConstantsFields, "gGodRay");
}
