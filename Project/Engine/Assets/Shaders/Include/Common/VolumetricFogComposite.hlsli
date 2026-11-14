#pragma once
#ifndef VOLUMETRIC_FOG_COMPOSITE_HLSLI
#define VOLUMETRIC_FOG_COMPOSITE_HLSLI

/// @file VolumetricFogComposite.hlsli
/// @brief ボリュメトリックの格子とその先の霧を色へ合成する
/// @details 全画面の合成（HeightFog.CS.hlsl）と前方描画（FogForward.hlsli）が同じ関数を呼ぶ。
///          格子が届く距離までは積み上げた格子を引く。その先は、高さフォグの FogOpticalDepth の濃さと、
///          格子より奥まで届く局所の霧（視線に沿って点で調べる）を足し、
///          格子の奥の光の画像（LightScattering が格子の縦横 1 列ずつ書く・物の影だけ無い同じ光の源）で照らす。
///          格子の値は FogParameters の volumetric* から読む（C++ 側が格子を作ったときの値を写す）。

#include "Fog.hlsli"
#include "VolumetricFogCommon.hlsli"

/// @brief 1 なら格子より奥まで届く局所の霧を視線に沿って足す（0 の版は調べない）
#ifndef VOLUMETRIC_FOG_FAR_VOLUMES
#define VOLUMETRIC_FOG_FAR_VOLUMES 1
#endif

/// @brief 格子より奥の高さフォグのアルベドと局所の霧（格子を使うフレームだけ差す）
ConstantBuffer<VolumetricFogFarMedia> gVolumetricFogFarMedia : register(b0, space6);

/// @brief 背景（深度 far）の画素で、局所の霧を調べる視線の長さ [m]
static const float kVolumetricFogSkyRayLength = 1.0e6f;

/// @brief 格子の UV を端の画素の中心までに収める
/// @details 線形のサンプラの繰り返し・端で止めるのどちらで引いても、端で止めると同じ値になる
float2 VolumetricFogClampGridUV(FogParameters fog, float2 gridUV)
{
    const float2 halfTexel = 0.5f / max(fog.volumetricGridSize.xy, 1.0f);
    return clamp(gridUV, halfTexel, 1.0f - halfTexel);
}

/// @brief 区間 [t0, t1] で、格子より奥まで届く局所の霧を視線に沿って足す（ノイズは掛けない）
/// @param opticalDepth 光学的深さ（消散係数 × 長さ）
/// @param scatterDepth 散乱の割合を掛けた光学的深さ
/// @param emission 視線に沿って足した発光（減衰を掛ける前）
void VolumetricFogMarchFarVolumes(
    float3 origin, float3 direction, float t0, float t1,
    out float opticalDepth, out float3 scatterDepth, out float3 emission)
{
    opticalDepth = 0.0f;
    scatterDepth = float3(0.0f, 0.0f, 0.0f);
    emission = float3(0.0f, 0.0f, 0.0f);

    [loop]
    for (uint i = 0; i < gVolumetricFogFarMedia.volumeCount; ++i)
    {
        const VolumetricFogLocalVolume volume = gVolumetricFogFarMedia.volumes[i];
        const float3 localOrigin = VolumetricFogVolumeLocal(volume, origin);
        const float3 localDir = VolumetricFogVolumeLocalDirection(volume, direction);
        float enter;
        float exit;
        if (!VolumetricFogVolumeRayRange(volume, localOrigin, localDir, enter, exit))
        {
            continue;
        }
        enter = max(enter, t0);
        exit = min(exit, t1);
        if (exit <= enter)
        {
            continue;
        }

        const float stepLength = (exit - enter) / float(kVolumetricFogFarVolumeSteps);
        float weightSum = 0.0f;
        [loop]
        for (uint k = 0; k < kVolumetricFogFarVolumeSteps; ++k)
        {
            const float t = enter + (float(k) + 0.5f) * stepLength;
            weightSum += VolumetricFogVolumeWeightLocal(volume, localOrigin + localDir * t);
        }
        const float insideLength = weightSum * stepLength;
        const float depth = volume.extinction * insideLength;
        opticalDepth += depth;
        scatterDepth += volume.albedo * depth;
        emission += volume.emissive * insideLength;
    }
}

/// @brief カメラから rayEnd までの霧の光と透過率（最大濃度を掛ける前）
/// @param integrated 積み上げた格子（rgb = 光・a = 透過率）
/// @param farLight 格子の奥の光の画像（rgb = 格子の端の点の光の源）
/// @param gridUV 格子の UV（画面の UV × screenToGrid）
/// @param rayDirection カメラから見た正規化済みの視線
/// @param rayEnd 面までの距離 [m]
/// @return rgb = 溜まった光、a = 透過率
/// @details 格子より奥は、光の源を区間の中で一定とみなし、高さフォグと局所の霧の光学的深さの和で減衰させる。
///          volumetricEnabled が kFogVolumetricLocalOnly のフレームは高さフォグを足さない（呼び出し側が解析式で掛ける）。
float4 VolumetricFogSampleRay(
    FogParameters fog, Texture3D<float4> integrated, Texture2D<float4> farLight, SamplerState linearSampler,
    float2 gridUV, float3 rayDirection, float rayEnd)
{
    const float2 uv = VolumetricFogClampGridUV(fog, gridUV);
    const float volumeEnd = min(rayEnd, fog.volumetricDistance);
    const float slice = VolumetricFogDistanceToSliceFrom(
        fog.volumetricDistance, fog.volumetricDepthDistribution, fog.volumetricGridSize.z, volumeEnd);
    const float4 volume = VolumetricFogSampleIntegratedAt(
        integrated, linearSampler, uv, slice, fog.volumetricGridSize.z);

    float3 farScattered = float3(0.0f, 0.0f, 0.0f);
    float farTransmittance = 1.0f;
    if (rayEnd > fog.volumetricDistance)
    {
        float heightDepth = 0.0f;
        if (fog.volumetricEnabled == kFogVolumetricHeightFog)
        {
            const float farStart = max(fog.volumetricDistance, fog.startDistance);
            heightDepth = FogOpticalDepth(fog, fog.cameraWorldPos, rayDirection, farStart, rayEnd);
        }
        float volumeDepth = 0.0f;
        float3 volumeScatterDepth = float3(0.0f, 0.0f, 0.0f);
        float3 emission = float3(0.0f, 0.0f, 0.0f);
#if VOLUMETRIC_FOG_FAR_VOLUMES
        VolumetricFogMarchFarVolumes(fog.cameraWorldPos, rayDirection, fog.volumetricDistance, rayEnd,
            volumeDepth, volumeScatterDepth, emission);
#endif

        const float depth = heightDepth + volumeDepth;
        farTransmittance = exp(-depth);
        // 区間の中で光の源と発光が一定なら、減衰した分の割合は (1 - T) / τ
        const float spread = (depth > 1.0e-4f) ? (1.0f - farTransmittance) / depth : 1.0f - 0.5f * depth;
        const float3 source = farLight.SampleLevel(linearSampler, uv, 0).rgb;
        farScattered = (source * (gVolumetricFogFarMedia.heightAlbedo * heightDepth + volumeScatterDepth) + emission)
                     * spread;
    }

    return float4(farScattered * volume.a + volume.rgb, farTransmittance * volume.a);
}

/// @brief 霧の光と透過率を色へ掛ける（高さフォグを格子で描くフレーム）
/// @param ray VolumetricFogSampleRay の値
/// @details 最大濃度は透過率を解析式のフォグと同じ式で持ち上げ、散乱した光も同じ割合で減らす。
///          光は volumetricLightScale を掛けて足す（加算・スクリーンの前方描画は 0 で減衰だけ）。
float3 VolumetricFogApplyRay(FogParameters fog, float4 ray, float3 color)
{
    const float maxOpacity = saturate(fog.maxOpacity);
    const float limitedTransmittance = lerp(1.0f, ray.a, maxOpacity);
    const float opacity = 1.0f - ray.a;
    const float lightScale = (opacity > 1.0e-5f) ? (1.0f - limitedTransmittance) / opacity : maxOpacity;
    return color * limitedTransmittance + ray.rgb * (lightScale * fog.volumetricLightScale);
}

/// @brief 局所の霧だけの光と透過率を色へ重ねる（高さフォグの最大濃度は掛けない）
/// @param ray VolumetricFogSampleRay の値
/// @param color 高さフォグを解析式で掛けた後の色
float3 VolumetricFogApplyLocalRay(FogParameters fog, float4 ray, float3 color)
{
    return color * ray.a + ray.rgb * fog.volumetricLightScale;
}

/// @brief volumetricEnabled に合わせて霧の光と透過率を色へ掛ける
/// @param color 局所の霧だけのフレームは、高さフォグを解析式で掛けた後の色
float3 VolumetricFogApplyAny(FogParameters fog, float4 ray, float3 color)
{
    if (fog.volumetricEnabled == kFogVolumetricLocalOnly)
    {
        return VolumetricFogApplyLocalRay(fog, ray, color);
    }
    return VolumetricFogApplyRay(fog, ray, color);
}

#endif // VOLUMETRIC_FOG_COMPOSITE_HLSLI
