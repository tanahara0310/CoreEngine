/// @file MultiScatteringLUT.CS.hlsl
/// @brief 多重散乱 LUT の事前計算（Hillaire 2020）
/// @details 各テクセル = (太陽天頂角余弦, 高度) に対する無限次数の多重散乱寄与 Psi_ms。
///          球面上の方向サンプリングで2次散乱と再散乱率を求め、
///          等方位相近似のもと幾何級数 L2 / (1 - f_ms) で無限次数へ外挿する。

#include "Common/AtmosphereCommon.hlsli"

ConstantBuffer<AtmosphereConstants> gAtmosphere : register(b0);
Texture2D<float4> gTransmittanceLUT : register(t0);
SamplerState gLUTSampler : register(s0);
RWTexture2D<float4> gMultiScatteringLUT : register(u0);

// 球面サンプル数（8x8 = 64方向）
static const uint SQRT_SAMPLE_COUNT = 8;

/// @brief 1方向分の寄与を積分する
/// @param rayOrigin サンプル点（惑星中心基準 [km]）
/// @param rayDir 積分方向
/// @param toSun 太陽の位置方向
/// @param outLuminance 2次散乱の元になる輝度（等方位相 1/4π 適用済み）
/// @param outMultiScatFactor 再散乱率（散乱された光がさらに散乱される割合）
void IntegrateDirection(float3 rayOrigin, float3 rayDir, float3 toSun,
                        out float3 outLuminance, out float3 outMultiScatFactor)
{
    outLuminance = float3(0.0f, 0.0f, 0.0f);
    outMultiScatFactor = float3(0.0f, 0.0f, 0.0f);

    float tTop = RaySphereIntersectNearest(rayOrigin, rayDir, gAtmosphere.atmosphereTopRadiusKm);
    if (tTop < 0.0f)
    {
        return;
    }

    float tMax = tTop;
    float tGround = RaySphereIntersectNearest(rayOrigin, rayDir, gAtmosphere.planetRadiusKm);
    bool hitGround = (tGround >= 0.0f);
    if (hitGround)
    {
        tMax = tGround;
    }

    const int kStepCount = 20;
    float dt = tMax / kStepCount;
    const float uniformPhase = 1.0f / (4.0f * ATMOSPHERE_PI);

    float3 throughput = float3(1.0f, 1.0f, 1.0f);

    for (int i = 0; i < kStepCount; ++i)
    {
        float3 p = rayOrigin + rayDir * ((i + 0.5f) * dt);
        float heightKm = length(p) - gAtmosphere.planetRadiusKm;

        float3 density = ComputeMediumDensity(heightKm, gAtmosphere);
        float3 scattering = gAtmosphere.rayleighScattering * density.x
                          + gAtmosphere.mieScattering.xxx * density.y;

        float3 sigmaT = max(ComputeExtinction(heightKm, gAtmosphere), 1e-7f);
        float3 stepTransmittance = exp(-sigmaT * dt);

        float3 transmittanceToSun = SampleTransmittanceToSun(
            gTransmittanceLUT, gLUTSampler, p, toSun, gAtmosphere);

        // ステップ内解析積分（AtmosphereCommon.hlsli の積分器と同じエネルギー保存形式）
        // 2次散乱の元: 太陽光がこの点で等方的に散乱される量
        float3 S2 = transmittanceToSun * scattering * uniformPhase;
        outLuminance += throughput * (S2 - S2 * stepTransmittance) / sigmaT;

        // 再散乱率: この方向に散乱された光がさらに散乱される割合
        outMultiScatFactor += throughput * (scattering - scattering * stepTransmittance) / sigmaT;

        throughput *= stepTransmittance;
    }

    // 地面に当たる場合はアルベド反射も2次光源として加える
    if (hitGround)
    {
        float3 groundPoint = rayOrigin + rayDir * tGround;
        float3 groundNormal = normalize(groundPoint);
        float NdotL = saturate(dot(groundNormal, toSun));
        float3 transmittanceToSun = SampleTransmittanceToSun(
            gTransmittanceLUT, gLUTSampler, groundPoint, toSun, gAtmosphere);
        outLuminance += throughput * transmittanceToSun * NdotL
                      * gAtmosphere.groundAlbedo / ATMOSPHERE_PI;
    }
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= MULTISCATTERING_LUT_SIZE || dispatchThreadId.y >= MULTISCATTERING_LUT_SIZE)
    {
        return;
    }

    float2 uv = (float2(dispatchThreadId.xy) + 0.5f) / float2(MULTISCATTERING_LUT_SIZE, MULTISCATTERING_LUT_SIZE);

    float muSun;
    float radiusKm;
    UvToMultiScatteringParams(uv, gAtmosphere.planetRadiusKm, gAtmosphere.atmosphereTopRadiusKm,
                              muSun, radiusKm);

    // 地表すれすれ・大気圏上端すれすれを避ける
    radiusKm = clamp(radiusKm,
        gAtmosphere.planetRadiusKm + 0.001f,
        gAtmosphere.atmosphereTopRadiusKm - 0.001f);

    float3 samplePos = float3(0.0f, radiusKm, 0.0f);
    float3 toSun = normalize(float3(sqrt(max(0.0f, 1.0f - muSun * muSun)), muSun, 0.0f));

    // 球面上の全方向について2次散乱と再散乱率を積分する
    float3 luminance2ndOrder = float3(0.0f, 0.0f, 0.0f);
    float3 multiScatFactor = float3(0.0f, 0.0f, 0.0f);

    const float sampleWeight = 1.0f / (SQRT_SAMPLE_COUNT * SQRT_SAMPLE_COUNT);

    for (uint i = 0; i < SQRT_SAMPLE_COUNT; ++i)
    {
        for (uint j = 0; j < SQRT_SAMPLE_COUNT; ++j)
        {
            // 球面を均等分割（面積要素 sinθ を考慮した一様サンプリング）
            float randA = (i + 0.5f) / SQRT_SAMPLE_COUNT;
            float randB = (j + 0.5f) / SQRT_SAMPLE_COUNT;
            float theta = acos(1.0f - 2.0f * randA); // cosθ を一様に
            float phi = 2.0f * ATMOSPHERE_PI * randB;

            float3 rayDir = float3(
                sin(theta) * cos(phi),
                cos(theta),
                sin(theta) * sin(phi));

            float3 dirLuminance;
            float3 dirMultiScat;
            IntegrateDirection(samplePos, rayDir, toSun, dirLuminance, dirMultiScat);

            // 等方位相での球面積分: (1/4π)∫f dω = 平均値（一様サンプリング）
            luminance2ndOrder += dirLuminance * sampleWeight;
            multiScatFactor += dirMultiScat * sampleWeight;
        }
    }

    // 幾何級数による無限次数外挿: Psi_ms = L2 / (1 - f_ms)
    float3 psiMs = luminance2ndOrder / max(1.0f - multiScatFactor, 1e-4f);

    gMultiScatteringLUT[dispatchThreadId.xy] = float4(psiMs, 1.0f);
}
