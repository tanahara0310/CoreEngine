/// @file HeightFog.CS.hlsl
/// @brief 高さフォグを SceneColor へ in-place 合成する
/// @details SceneDepth からワールド座標を復元し、カメラからその点までの透過率で
///          シーン色を内散乱色へ寄せる。式: 出力 = lerp(内散乱色, シーン色, 透過率)。
///          gOutput は SceneColor 自身。各スレッドが自分のテクセルだけを読んで書き戻すので
///          中間バッファは要らない（GodRayComposite.CS.hlsl と同じ形）。
///
/// 背景（深度 far）は「skyDistance まで進むレイ」として扱う。
/// heightFalloff > 0 なら上向きレイは光学的深さが収束するので空は自然に薄く、
/// 下向きレイ（地面の穴・世界の縁）は発散するのでフォグ色で埋まる。
///
/// 空色ブレンドが有効なフレームは Sky-View LUT を視線方向で引き、フォグ色を空へ寄せる。
/// 大気が無い（LUT 未生成の）シーンでは C++ 側が skyColorBlend = 0 にするため、
/// LUT のサンプルそのものが走らない。

#include "Fog.hlsli"
#include "DepthReconstruction.hlsli"
#include "Common/AtmosphereCommon.hlsli" // SkyViewAzimuth / SkyViewParamsToUv

ConstantBuffer<FogParameters> gFog : register(b0);
Texture2D<float> gSceneDepth : register(t0);
Texture2D<float4> gSkyViewLUT : register(t1);
SamplerState gLUTSampler : register(s0);
RWTexture2D<float4> gOutput : register(u0);

/// @brief 視線方向の空の輝度を Sky-View LUT から取得する
/// @details マッピングは SkyEnvironmentCapture.CS / SkyIrradianceSH.CS と同一。
///          ずらすとフォグ色と実際の空の色が食い違う。
float3 SampleSkyLuminance(float3 dir)
{
    const float radiusKm = max(gFog.cameraRadiusKm, gFog.planetRadiusKm + 0.001f);

    const float viewZenithCos = dir.y;
    const float viewAzimuth = SkyViewAzimuth(dir);

    const float cosHorizon = -sqrt(max(0.0f,
        1.0f - (gFog.planetRadiusKm * gFog.planetRadiusKm) / (radiusKm * radiusKm)));
    const bool intersectGround = (viewZenithCos < cosHorizon);

    const float2 uv = SkyViewParamsToUv(intersectGround, viewZenithCos, viewAzimuth,
                                        radiusKm, gFog.planetRadiusKm);

    // LUT はライト色・強度前乗算済み（SkyAtmosphere.PS と同じ輝度ドメイン）
    return gSkyViewLUT.SampleLevel(gLUTSampler, uv, 0).rgb;
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint width;
    uint height;
    gOutput.GetDimensions(width, height);
    if (dispatchThreadId.x >= width || dispatchThreadId.y >= height)
    {
        return;
    }

    const float ndcDepth = gSceneDepth[dispatchThreadId.xy];
    const bool isBackground = IsBackgroundDepth(ndcDepth);

    // 空へ掛けない設定なら背景ピクセルには触れない
    if (isBackground && gFog.applyToSky == 0)
    {
        return;
    }

    const float2 uv = (float2(dispatchThreadId.xy) + 0.5f) / float2(width, height);
    const float3 worldPos = ReconstructWorldPosition(
        ScreenUVToNDC(uv), ndcDepth, gFog.invViewProj);

    const float3 toSurface = worldPos - gFog.cameraWorldPos;
    const float surfaceDistance = length(toSurface);
    if (surfaceDistance < 1.0e-5f)
    {
        return;
    }
    const float3 rayDirection = toSurface / surfaceDistance;

    // 背景の復元点はファークリップ上なので、レイ長は skyDistance で置き換える
    const float rayEnd = isBackground ? max(gFog.skyDistance, 0.0f) : surfaceDistance;
    const float rayStart = min(gFog.startDistance, rayEnd);

    const float transmittance = FogTransmittance(
        gFog, gFog.cameraWorldPos, rayDirection, rayStart, rayEnd);

    // 分岐は定数バッファ由来なのでワープ内で一様。LUT 未バインドのフレームは
    // skyColorBlend が 0 なのでサンプルへ入らない
    float3 skyLuminance = gFog.fogColor;
    if (gFog.skyColorBlend > 0.0f)
    {
        skyLuminance = SampleSkyLuminance(rayDirection);
    }
    const float3 inscattering = FogInscatteringColor(gFog, rayDirection, skyLuminance);

    const float4 sceneColor = gOutput[dispatchThreadId.xy];
    gOutput[dispatchThreadId.xy] = float4(
        lerp(inscattering, sceneColor.rgb, transmittance), sceneColor.a);
}
