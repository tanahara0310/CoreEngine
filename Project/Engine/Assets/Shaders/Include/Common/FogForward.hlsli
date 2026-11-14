#pragma once
#ifndef FOG_FORWARD_HLSLI
#define FOG_FORWARD_HLSLI

/// @file FogForward.hlsli
/// @brief 前方描画（半透明・水面・パーティクル）の霧
/// @details gFog は呼び出し側のシェーダーが自分の空いたレジスタで宣言する。
///          ボリュメトリックの格子と格子より奥の局所の霧は space6 に置き、全画面の合成と同じ
///          VolumetricFogComposite.hlsli の式で引く。
///          格子を使わないフレーム（kFogVolumetricOff）は Fog.hlsli の解析式のまま。
///          局所の霧だけのフレーム（kFogVolumetricLocalOnly）は、解析式の高さフォグの手前に局所の霧を重ねる。

#include "Fog.hlsli"
#include "VolumetricFogComposite.hlsli"

Texture3D<float4> gVolumetricFogIntegrated : register(t0, space6); ///< 積み上げた格子（rgb = 光・a = 透過率）
Texture2D<float4> gVolumetricFogFarLight : register(t1, space6);   ///< 格子の奥の光の画像
SamplerState gVolumetricFogSampler : register(s0, space6);         ///< 線形（UV は端の画素の中心までに収めて引く）

/// @brief ワールド座標が分かっている面の色へ霧を合成する
float3 ApplyForwardFog(FogParameters p, float3 worldPos, float3 color)
{
    if (p.volumetricEnabled == kFogVolumetricOff)
    {
        return ApplyFog(p, worldPos, color);
    }

    const float3 toSurface = worldPos - p.cameraWorldPos;
    const float distance = length(toSurface);
    if (distance < 1.0e-5f)
    {
        return color;
    }

    // 格子を作ったジッタ無しの行列で画面へ写す（前方描画の面はカメラの前にある）
    const float4 clip = mul(float4(worldPos, 1.0f), p.volumetricViewProj);
    const float2 ndc = clip.xy / max(clip.w, 1.0e-5f);
    const float2 screenUV = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);

    const float4 ray = VolumetricFogSampleRay(
        p, gVolumetricFogIntegrated, gVolumetricFogFarLight, gVolumetricFogSampler,
        screenUV * p.volumetricScreenToGrid, toSurface / distance, distance);
    if (p.volumetricEnabled == kFogVolumetricLocalOnly)
    {
        return VolumetricFogApplyLocalRay(p, ray, ApplyFog(p, worldPos, color));
    }
    return VolumetricFogApplyRay(p, ray, color);
}

#endif // FOG_FORWARD_HLSLI
