/// @file TransmittanceLUT.CS.hlsl
/// @brief 大気透過率 LUT の事前計算（Bruneton/Hillaire 方式）
/// @details 各テクセル = (高度, 天頂角余弦) に対する「その点から大気圏上端までの透過率」。
///          パラメータ変更時のみ再計算される（AtmosphereManager のダーティフラグ管理）。

#include "Common/AtmosphereCommon.hlsli"

ConstantBuffer<AtmosphereConstants> gAtmosphere : register(b0);
RWTexture2D<float4> gTransmittanceLUT : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= TRANSMITTANCE_LUT_WIDTH || dispatchThreadId.y >= TRANSMITTANCE_LUT_HEIGHT)
    {
        return;
    }

    float2 uv = (float2(dispatchThreadId.xy) + 0.5f) / float2(TRANSMITTANCE_LUT_WIDTH, TRANSMITTANCE_LUT_HEIGHT);

    float radiusKm;
    float mu;
    UvToTransmittanceParams(uv, gAtmosphere.planetRadiusKm, gAtmosphere.atmosphereTopRadiusKm,
                            radiusKm, mu);

    // レイ設定: 惑星中心の真上 (0, r, 0) から天頂角余弦 mu の方向へ
    float3 rayOrigin = float3(0.0f, radiusKm, 0.0f);
    float3 rayDir = float3(sqrt(max(0.0f, 1.0f - mu * mu)), mu, 0.0f);

    // このパラメータ化ではレイは必ず大気圏上端で抜ける
    float tTop = RaySphereIntersectNearest(rayOrigin, rayDir, gAtmosphere.atmosphereTopRadiusKm);
    tTop = max(tTop, 0.0f);

    const int kStepCount = 64;
    float dt = tTop / kStepCount;

    float3 opticalDepth = float3(0.0f, 0.0f, 0.0f);
    for (int i = 0; i < kStepCount; ++i)
    {
        float3 p = rayOrigin + rayDir * ((i + 0.5f) * dt);
        float heightKm = length(p) - gAtmosphere.planetRadiusKm;
        opticalDepth += ComputeExtinction(heightKm, gAtmosphere) * dt;
    }

    gTransmittanceLUT[dispatchThreadId.xy] = float4(exp(-opticalDepth), 1.0f);
}
