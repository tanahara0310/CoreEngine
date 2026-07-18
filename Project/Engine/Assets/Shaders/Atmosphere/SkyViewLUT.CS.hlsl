/// @file SkyViewLUT.CS.hlsl
/// @brief Sky-View LUT の生成（Hillaire 2020 / パラメータ化は UE 方式の絶対方位）
/// @details 各テクセル = カメラ視点からの (視線天頂角, 絶対方位角) に対する空の放射輝度。
///          ライト色・強度（sunColor × sunIntensity）は前乗算して格納する
///          （第2大気ライト＝月の寄与を将来同じ LUT へ合算するための輝度ドメイン）。
///          本番描画（SkyAtmosphere.PS.hlsl）はこの LUT を1回サンプリングするだけになる。
///          太陽方向・色・強度・カメラ高度・大気パラメータの変化時のみ再計算される。

#include "Common/AtmosphereCommon.hlsli"

ConstantBuffer<AtmosphereConstants> gAtmosphere : register(b0);
Texture2D<float4> gTransmittanceLUT : register(t0);
Texture2D<float4> gMultiScatteringLUT : register(t1);
SamplerState gLUTSampler : register(s0);
RWTexture2D<float4> gSkyViewLUT : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= SKYVIEW_LUT_WIDTH || dispatchThreadId.y >= SKYVIEW_LUT_HEIGHT)
    {
        return;
    }

    float2 uv = (float2(dispatchThreadId.xy) + 0.5f) / float2(SKYVIEW_LUT_WIDTH, SKYVIEW_LUT_HEIGHT);

    float radiusKm = clamp(gAtmosphere.cameraRadiusKm,
        gAtmosphere.planetRadiusKm + 0.001f,
        gAtmosphere.atmosphereTopRadiusKm - 0.001f);

    float viewZenithCos;
    float viewAzimuth;
    UvToSkyViewParams(uv, radiusKm, gAtmosphere.planetRadiusKm, viewZenithCos, viewAzimuth);

    // 大気空間: カメラは +Y 軸上（天頂 = ワールド +Y）。方位は絶対方位なので
    // 太陽方向はワールドの値をそのまま使う（旧実装のような X 側への再配置はしない）
    float3 toSun = -gAtmosphere.sunDirection;

    float viewZenithSin = sqrt(max(0.0f, 1.0f - viewZenithCos * viewZenithCos));
    float3 rayDir = float3(
        viewZenithSin * cos(viewAzimuth),
        viewZenithCos,
        viewZenithSin * sin(viewAzimuth));

    float3 rayOrigin = float3(0.0f, radiusKm, 0.0f);

    // ステップ数は経路長に比例させる。地平線際のレイは経路が 1000km 超になり、
    // 固定 40 ステップでは 1 ステップ約 30km となって低高度の濃い散乱を解像できない
    // （日没の地平線グラデーションの精度に直結する）。天頂方向（~80km）は少なくてよい
    float tTop = RaySphereIntersectNearest(rayOrigin, rayDir, gAtmosphere.atmosphereTopRadiusKm);
    float tGround = RaySphereIntersectNearest(rayOrigin, rayDir, gAtmosphere.planetRadiusKm);
    float pathKm = (tGround >= 0.0f) ? tGround : max(tTop, 0.0f);
    const int stepCount = (int)lerp(16.0f, 64.0f, saturate(pathKm / 150.0f));

    float3 luminance = IntegrateScatteredLuminance(
        rayOrigin, rayDir, toSun, gAtmosphere,
        gTransmittanceLUT, gMultiScatteringLUT, gLUTSampler, stepCount);

    // ライト色・強度を前乗算して「最終放射輝度」で格納する（消費側は乗算しない）
    luminance *= gAtmosphere.sunColor * gAtmosphere.sunIntensity;

    // 月（第2大気ライト）の散乱寄与を合算する（UE の Second Atmosphere Light 相当）。
    // Transmittance / MultiScattering LUT はライト非依存（高度×天頂角）なのでそのまま共用できる
    if (gAtmosphere.hasMoon > 0.5f)
    {
        float3 toMoon = -gAtmosphere.moonDirection;
        luminance += IntegrateScatteredLuminance(
            rayOrigin, rayDir, toMoon, gAtmosphere,
            gTransmittanceLUT, gMultiScatteringLUT, gLUTSampler, stepCount)
            * gAtmosphere.moonColor * gAtmosphere.moonIntensity;
    }

    gSkyViewLUT[dispatchThreadId.xy] = float4(luminance, 1.0f);
}
