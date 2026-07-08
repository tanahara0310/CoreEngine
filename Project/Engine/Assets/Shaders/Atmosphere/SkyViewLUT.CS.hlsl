/// @file SkyViewLUT.CS.hlsl
/// @brief Sky-View LUT の生成（Hillaire 2020）
/// @details 各テクセル = カメラ視点からの (視線天頂角, 太陽相対方位角) に対する空の輝度。
///          本番描画（SkyAtmosphere.PS.hlsl）はこの LUT を1回サンプリングするだけになる。
///          太陽方向・カメラ高度・大気パラメータの変化時のみ再計算される。

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
    float lightViewCos;
    UvToSkyViewParams(uv, radiusKm, gAtmosphere.planetRadiusKm, viewZenithCos, lightViewCos);

    // 大気空間: カメラは +Y 軸上、太陽は XZ 平面内の X 側に配置（LUT は太陽相対方位で格納）
    float muSun = dot(float3(0.0f, 1.0f, 0.0f), -gAtmosphere.sunDirection);
    float3 toSun = normalize(float3(sqrt(max(0.0f, 1.0f - muSun * muSun)), muSun, 0.0f));

    float viewZenithSin = sqrt(max(0.0f, 1.0f - viewZenithCos * viewZenithCos));
    float3 rayDir = float3(
        viewZenithSin * lightViewCos,
        viewZenithCos,
        viewZenithSin * sqrt(max(0.0f, 1.0f - lightViewCos * lightViewCos)));

    float3 rayOrigin = float3(0.0f, radiusKm, 0.0f);

    const int kStepCount = 40;
    float3 luminance = IntegrateScatteredLuminance(
        rayOrigin, rayDir, toSun, gAtmosphere,
        gTransmittanceLUT, gMultiScatteringLUT, gLUTSampler, kStepCount);

    gSkyViewLUT[dispatchThreadId.xy] = float4(luminance, 1.0f);
}
