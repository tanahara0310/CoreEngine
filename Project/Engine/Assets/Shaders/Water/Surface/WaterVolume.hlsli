// ============================================================
// 水の体積色（Water.PS.hlsl 専用）
// ------------------------------------------------------------
// 天空光 SH 評価・水中インスキャッタ環境光・Beer-Lambert＋単一散乱の体積色・
// RT 屈折の透過色解決。水柱厚さ（WaterColumn.hlsli）と対で使う。
// ============================================================
#ifndef WATER_VOLUME_INCLUDED
#define WATER_VOLUME_INCLUDED

#include "WaterSurfaceResources.hlsli"

/// @brief SH9 係数から法線方向の空の放射照度/π を評価する
/// @details DeferredLighting.PS.hlsl の EvaluateSkyIrradiance と同じ評価式。
///          係数（gWaterSkyIrradianceSH）には放射照度畳み込みと太陽色・強度が焼き込み済み。
float3 EvaluateWaterSkyIrradiance(float3 n)
{
    float basis[9];
    basis[0] = 0.282095f;
    basis[1] = 0.488603f * n.y;
    basis[2] = 0.488603f * n.z;
    basis[3] = 0.488603f * n.x;
    basis[4] = 1.092548f * n.x * n.y;
    basis[5] = 1.092548f * n.y * n.z;
    basis[6] = 0.315392f * (3.0f * n.z * n.z - 1.0f);
    basis[7] = 1.092548f * n.x * n.z;
    basis[8] = 0.546274f * (n.x * n.x - n.y * n.y);

    float3 irradiance = float3(0.0f, 0.0f, 0.0f);
    [unroll]
    for (int i = 0; i < 9; ++i)
    {
        irradiance += gWaterSkyIrradianceSH[i].rgb * basis[i];
    }
    // SH の帯域打ち切りによる負のリンギングを防ぐ
    return max(irradiance, 0.0f);
}

/// @brief 水中インスキャッタリングの環境光（水面に入射する光の放射輝度近似）を求める
/// @details 平行光源（太陽）の下向き放射照度と天空拡散光を合算する。
///          太陽ライトの GPU 転送色には LightManager が大気の Transmittance LUT による
///          減衰（日没の赤方偏移・減光）を乗算済みのため、ここで読むだけで大気に追従する。
///          天空光は大気アクティブ時の Sky Irradiance SH（時刻・太陽高度に連動）。
///          非アクティブ時は天空光を足さない。
/// @param mainLightVisibility メインライト（0 番）の日向率（0=影 / 1=日向）。
///        影になった水面の下の水柱には太陽の光が入らないので、太陽の分だけに掛ける
float3 ComputeUnderwaterAmbientLight(float mainLightVisibility)
{

    // 太陽など平行光源: 水平な水面へ入る下向き放射照度を Lambert 面の放射輝度へ換算（/π）
    // downwelling は太陽高度に対してほぼ線形に 0 へ落ちる（sin(elevation) 相当）ため、
    // 太陽を下げていくと直射成分だけが急激に暗転する。
    float3 sunAmbient = float3(0.0f, 0.0f, 0.0f);
    for (uint i = 0; i < gLightCounts.directionalLightCount; ++i)
    {
        if (gDirectionalLights[i].enabled == 0)
        {
            continue;
        }
        float downwelling = saturate(-normalize(gDirectionalLights[i].direction).y);
        const float visibility = (i == 0) ? mainLightVisibility : 1.0f;
        sunAmbient += gDirectionalLights[i].color.rgb * gDirectionalLights[i].intensity * downwelling
            * visibility / PI;
    }

    // 天空拡散光
    float3 skyAmbient = float3(0.0f, 0.0f, 0.0f);
    if (gSkyAmbientEnabled != 0)
    {
        // 大気散乱の空を SH9 で評価（上向き法線＝水面へ降り注ぐ天空放射照度/π）。
        // 水面に映る空（空キューブマップ）と同じ輝度の単位のまま使い、
        // 地面の環境光に掛ける gSkyAmbientScale は掛けない
        skyAmbient = EvaluateWaterSkyIrradiance(float3(0.0f, 1.0f, 0.0f));
    }

    // 水中に入る光と水面に映る空は同じ空なので、天空光はそのまま足す
    return sunAmbient + skyAmbient;
}

// 太陽から海底までの下り光路の吸収は、DeferredLighting の水中ライティング置換（RT コースティクス）が
// 海底の画素の直接光に exp(-σa·実光路長) として含めている。ここで掛けるのは視線の上り光路だけ。

/// @brief 水柱を通過した背景色に波長依存の吸収・散乱を適用する
/// @param refractionColor 水面越しに見える背景（海底・水中物体）の色
/// @param transmittance   exp(-σt·d) 視線（上り）光路の波長別透過率
/// @param sigmaS          散乱係数 σs [1/m]
/// @param sigmaT          消散係数 σt = σa + σs [1/m]
/// @param ambientLight    水面に入射する環境光（ComputeUnderwaterAmbientLight）
/// @details 透過項 + 均質媒質の単一散乱解析解。
///          浅瀬では transmittance がまだ緑・青を通すため海底アルベド（白砂）が
///          エメラルドに、深瀬では透過が消えて (σs/σt)·L の水固有の青に収束する。
float3 ComputeWaterVolumetricColor(
    float3 refractionColor, float3 transmittance,
    float3 sigmaS, float3 sigmaT, float3 ambientLight)
{
    float3 transmitted = refractionColor * transmittance;
    float3 inscatter = (sigmaS / sigmaT) * ambientLight * (1.0f - transmittance);
    return transmitted + inscatter;
}

/// @brief 水面越しに見える背景の色（RT 屈折の色。RT 屈折が無い画素はシーンの色）
/// @details RT 屈折の rgb は、屈折先のシーン色と屈折させない自分の画素のシーン色を
///          連続ブレンドした色なので、光路長が有効ならそのまま使う（IsRTColorValid では切り替えない）
float3 ResolveWaterTransmissionColor(uint2 pixelCoord, float2 screenUV)
{
    float4 rtRefraction = gRTWaterRefractionColor.Load(int3(pixelCoord, 0));
    if (IsRTPathValid(rtRefraction.a) > 0.5f) {
        return rtRefraction.rgb;
    }

    return gSceneColor.Sample(gLinearClamp, screenUV).rgb;
}

/// @brief 水面越しに見える水中の色（背景の色に、視線の上り光路の吸収と散乱を掛けた色）
/// @param transmittance       exp(-σt·d) 視線の上り光路の波長別透過率
/// @param mainLightVisibility メインライト（0 番）の日向率（影の中の光を残した値）
float3 ResolveWaterTransmission(
    uint2 pixelCoord, float2 screenUV, float3 transmittance,
    float3 sigmaS, float3 sigmaT, float mainLightVisibility)
{
    const float3 refractionColor = ResolveWaterTransmissionColor(pixelCoord, screenUV);
    const float3 underwaterAmbient = ComputeUnderwaterAmbientLight(mainLightVisibility);
    return ComputeWaterVolumetricColor(refractionColor, transmittance, sigmaS, sigmaT, underwaterAmbient);
}

/// @brief 水中の放射輝度が水面から空気へ出るときの倍率（1/n²）
float WaterRadianceExitScale()
{
    const float n = max(gWaterRefractiveIndex, 1.0e-4f);
    return 1.0f / (n * n);
}

#endif // WATER_VOLUME_INCLUDED
