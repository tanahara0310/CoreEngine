#pragma once
#ifndef VOLUMETRIC_FOG_COMPOSITE_HLSLI
#define VOLUMETRIC_FOG_COMPOSITE_HLSLI

/// @file VolumetricFogComposite.hlsli
/// @brief ボリュメトリックの格子とその先の解析的な霧を色へ合成する
/// @details 全画面の合成（HeightFog.CS.hlsl）と前方描画（FogForward.hlsli）が同じ関数を呼ぶ。
///          格子が届く距離までは積み上げた格子を引き、その先は FogOpticalDepth の濃さと、
///          格子の奥の光の画像（LightScattering が格子の縦横 1 列ずつ書く・物の影だけ無い同じ光の式）で足す。
///          格子の値は FogParameters の volumetric* から読む（C++ 側が格子を作ったときの値を写す）。

#include "Fog.hlsli"
#include "VolumetricFogCommon.hlsli"

/// @brief 格子の UV を端の画素の中心までに収める
/// @details 線形のサンプラの繰り返し・端で止めるのどちらで引いても、端で止めると同じ値になる
float2 VolumetricFogClampGridUV(FogParameters fog, float2 gridUV)
{
    const float2 halfTexel = 0.5f / max(fog.volumetricGridSize.xy, 1.0f);
    return clamp(gridUV, halfTexel, 1.0f - halfTexel);
}

/// @brief カメラから rayEnd までの霧の光と透過率（最大濃度を掛ける前）
/// @param integrated 積み上げた格子（rgb = 光・a = 透過率）
/// @param farLight 格子の奥の光の画像（rgb = アルベド × 格子の端の点の光の源）
/// @param gridUV 格子の UV（画面の UV × screenToGrid）
/// @param rayDirection カメラから見た正規化済みの視線
/// @param rayEnd 面までの距離 [m]
/// @return rgb = 溜まった光、a = 透過率
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
        const float farStart = max(fog.volumetricDistance, fog.startDistance);
        farTransmittance = exp(-FogOpticalDepth(fog, fog.cameraWorldPos, rayDirection, farStart, rayEnd));
        farScattered = farLight.SampleLevel(linearSampler, uv, 0).rgb * (1.0f - farTransmittance);
    }

    return float4(farScattered * volume.a + volume.rgb, farTransmittance * volume.a);
}

/// @brief 霧の光と透過率を色へ掛ける
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

#endif // VOLUMETRIC_FOG_COMPOSITE_HLSLI
