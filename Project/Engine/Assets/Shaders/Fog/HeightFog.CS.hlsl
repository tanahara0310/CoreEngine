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
///
/// volumetricEnabled のフレームは、格子が届く距離までをボリュメトリックの格子から引き、
/// その先を同じ濃さの式と同じ光の式で解析的に足す（雲の影は格子の端の 1 点で引き、物の影は掛けない）。

#include "Fog.hlsli"
#include "VolumetricFogCommon.hlsli"
#include "DepthReconstruction.hlsli"
#include "Common/AtmosphereCommon.hlsli" // SkyViewAzimuth / SkyViewParamsToUv

ConstantBuffer<FogParameters> gFog : register(b0);
ConstantBuffer<VolumetricFogParameters> gVolumetricFog : register(b1);
Texture2D<float> gSceneDepth : register(t0);
Texture2D<float4> gSkyViewLUT : register(t1);
Texture3D<float4> gVolumetricFogIntegrated : register(t2);
StructuredBuffer<float4> gSkyIrradianceSH : register(t3);
Texture2D<float> gCloudShadowMap : register(t4);
Texture2D<float> gSunDepth : register(t5);
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

/// @brief カメラが水上にいるとき、水面の範囲内で水面より下にある点か
/// @details この点は水越しに見えるので、空気のフォグは水面の描画が水面までの距離で掛ける
bool IsBelowWaterSurface(float3 worldPos)
{
    if (gFog.waterClipEnabled == 0 || gFog.cameraWorldPos.y <= gFog.waterHeight)
    {
        return false;
    }
    const float2 offset = abs(worldPos.xz - gFog.waterRegionCenterXZ);
    return worldPos.y < gFog.waterHeight && all(offset <= gFog.waterRegionHalfExtentXZ);
}

/// @brief ボリュメトリックの格子と、その先の解析的な霧をシーン色へ合成する
/// @param uv 画面の UV
/// @param rayDirection カメラから見た正規化済みの視線
/// @param rayEnd 面までの距離 [m]
float3 ApplyVolumetricFog(float2 uv, float3 rayDirection, float rayEnd, float3 sceneColor)
{
    const VolumetricFogParameters p = gVolumetricFog;
    const float volumeEnd = min(rayEnd, p.distance);
    const float4 volume = VolumetricFogSampleIntegrated(
        p, gVolumetricFogIntegrated, gLUTSampler, uv * p.screenToGrid, volumeEnd);

    float3 farLight = float3(0.0f, 0.0f, 0.0f);
    float farTransmittance = 1.0f;
    if (rayEnd > p.distance)
    {
        const float farStart = max(p.distance, gFog.startDistance);
        farTransmittance = exp(-FogOpticalDepth(gFog, gFog.cameraWorldPos, rayDirection, farStart, rayEnd));
        const float3 ambient = VolumetricFogSkyAmbient(p, gSkyIrradianceSH);
        const float3 boundary = gFog.cameraWorldPos + rayDirection * p.distance;
        const float3 source = VolumetricFogSource(
            p, rayDirection, ambient, boundary, gCloudShadowMap, gLUTSampler, 1.0f);
        farLight = p.albedo * source * (1.0f - farTransmittance);
    }

    const float transmittance = farTransmittance * volume.a;
    const float3 light = farLight * volume.a + volume.rgb;

    // 最大濃度: 透過率を今の高さフォグと同じ式で持ち上げ、散乱した光も同じ割合で減らす
    const float maxOpacity = saturate(gFog.maxOpacity);
    const float limitedTransmittance = lerp(1.0f, transmittance, maxOpacity);
    const float opacity = 1.0f - transmittance;
    const float lightScale = (opacity > 1.0e-5f) ? (1.0f - limitedTransmittance) / opacity : maxOpacity;

    if (p.debugView == 1)
    {
        return volume.aaa;
    }
    if (p.debugView == 2)
    {
        return volume.rgb;
    }
    if (p.debugView == 3)
    {
        const float slice = VolumetricFogDistanceToSlice(p, volumeEnd);
        const float band = frac(floor(slice) * 0.61803398875f);
        return float3(band, frac(band * 3.0f), 1.0f - band);
    }
    if (p.debugView == 4)
    {
        if (p.directionalCount == 0)
        {
            return float3(1.0f, 1.0f, 1.0f);
        }
        // 太陽から見た深さの画像を使う光源があればその光源、無ければ最初の光源
        const bool hasSunDepth = p.sunDepthLightIndex != kVolumetricFogNoSunDepthLight;
        const uint lightIndex = hasSunDepth ? p.sunDepthLightIndex : 0u;
        const float3 endPoint = gFog.cameraWorldPos + rayDirection * volumeEnd;
        const float cloudShadow = VolumetricFogCloudShadow(
            p, gCloudShadowMap, gLUTSampler, endPoint, -p.directional[lightIndex].direction);
        return (cloudShadow * VolumetricFogSunDepthShadow(p, gSunDepth, gLUTSampler, endPoint)).xxx;
    }
    if (p.debugView == 5)
    {
        // 太陽から見た深さの画像を画面の中央へ正方形で出す
        // （霧の範囲の光源側の端 = 黒・反対側の端と当たらない所 = 白・正方形の外 = 暗い灰色。画像が無ければ暗い赤）
        if (p.sunDepthLightIndex == kVolumetricFogNoSunDepthLight)
        {
            return float3(0.3f, 0.0f, 0.0f);
        }
        uint width;
        uint height;
        gOutput.GetDimensions(width, height);
        const float aspect = float(width) / max(float(height), 1.0f);
        const float2 imageUV = (uv - 0.5f) * float2(max(aspect, 1.0f), max(1.0f / aspect, 1.0f)) + 0.5f;
        if (any(imageUV < 0.0f) || any(imageUV > 1.0f))
        {
            return float3(0.05f, 0.05f, 0.05f);
        }
        const float regionSize = 1.0f / max(p.sunDepthInvSize, 1.0e-6f);
        const float regionFront = p.sunDepthRange - regionSize;
        const float depth = gSunDepth.SampleLevel(gLUTSampler, imageUV, 0);
        return saturate((depth - regionFront) / regionSize).xxx;
    }

    return sceneColor * limitedTransmittance + light * lightScale;
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
    if (!isBackground && IsBelowWaterSurface(worldPos))
    {
        return;
    }

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

    if (gFog.volumetricEnabled != 0)
    {
        const float4 volumetricScene = gOutput[dispatchThreadId.xy];
        gOutput[dispatchThreadId.xy] = float4(
            ApplyVolumetricFog(uv, rayDirection, rayEnd, volumetricScene.rgb), volumetricScene.a);
        return;
    }

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
