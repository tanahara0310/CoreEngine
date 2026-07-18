/// @file SkyIrradianceSH.CS.hlsl
/// @brief Sky-View LUT を2次球面調和（SH9）へ射影し、空の拡散環境光を生成する
/// @details UE の Sky Light (Real-Time Capture) の拡散成分に相当する。
///          全球 1024 方向から Sky-View LUT をサンプリングして SH9 係数を求め、
///          放射照度畳み込み（Ramamoorthi & Hanrahan 2001 の帯係数）まで済ませた
///          係数を 9 要素のバッファへ出力する。
///          DeferredLighting はこれを Σ c_i・Y_i(N) と評価するだけで
///          「法線 N に対する空からの放射照度 / π」（＝ランバート拡散に掛ける値）が得られる。
///          Sky-View LUT が再生成されたときのみ実行される（1グループ・Dispatch(1,1,1)）。

#include "Common/AtmosphereCommon.hlsli"

ConstantBuffer<AtmosphereConstants> gAtmosphere : register(b0);
Texture2D<float4> gSkyViewLUT : register(t0);
SamplerState gLUTSampler : register(s0);
RWStructuredBuffer<float4> gSkyIrradianceSH : register(u0); // 9 要素（rgb = 係数, a 未使用）

static const uint kGroupSize = 8;          // 8x8 = 64 スレッド
static const uint kSamplesPerThread = 4;   // 各スレッド 4x4 方向 → 全体 32x32 = 1024 方向
static const uint kDirGridSize = kGroupSize * kSamplesPerThread; // 32
static const uint kThreadCount = kGroupSize * kGroupSize;
static const uint kCoeffCount = 9;

// 各スレッドの SH 部分和（64 スレッド × 9 係数 × float3 ≒ 6.9KB）
groupshared float3 sCoeffSum[kThreadCount][kCoeffCount];

/// @brief 実数球面調和基底（2次・9係数）
void EvaluateSHBasis(float3 d, out float basis[kCoeffCount])
{
    basis[0] = 0.282095f;
    basis[1] = 0.488603f * d.y;
    basis[2] = 0.488603f * d.z;
    basis[3] = 0.488603f * d.x;
    basis[4] = 1.092548f * d.x * d.y;
    basis[5] = 1.092548f * d.y * d.z;
    basis[6] = 0.315392f * (3.0f * d.z * d.z - 1.0f);
    basis[7] = 1.092548f * d.x * d.z;
    basis[8] = 0.546274f * (d.x * d.x - d.y * d.y);
}

/// @brief 指定方向の空の輝度を Sky-View LUT から取得する（SkyAtmosphere.PS.hlsl と同じマッピング）
float3 SampleSkyLuminance(float3 dir)
{
    const float radiusKm = clamp(gAtmosphere.cameraRadiusKm,
        gAtmosphere.planetRadiusKm + 0.001f,
        gAtmosphere.atmosphereTopRadiusKm - 0.001f);

    const float viewZenithCos = dir.y;
    const float viewAzimuth = SkyViewAzimuth(dir);

    const float cosHorizon = -sqrt(max(0.0f,
        1.0f - (gAtmosphere.planetRadiusKm * gAtmosphere.planetRadiusKm) / (radiusKm * radiusKm)));
    const bool intersectGround = (viewZenithCos < cosHorizon);

    const float2 uv = SkyViewParamsToUv(intersectGround, viewZenithCos, viewAzimuth,
                                        radiusKm, gAtmosphere.planetRadiusKm);

    // LUT はライト色・強度前乗算済み（本番描画 SkyAtmosphere.PS と同じ輝度ドメイン）
    return gSkyViewLUT.SampleLevel(gLUTSampler, uv, 0).rgb;
}

[numthreads(kGroupSize, kGroupSize, 1)]
void main(uint3 groupThreadId : SV_GroupThreadID, uint groupIndex : SV_GroupIndex)
{
    float3 coeff[kCoeffCount];
    for (uint c = 0; c < kCoeffCount; ++c)
    {
        coeff[c] = float3(0.0f, 0.0f, 0.0f);
    }

    // 全球を一様サンプリング（cosθ を一様に取る。MultiScatteringLUT と同じ方法）
    for (uint j = 0; j < kSamplesPerThread; ++j)
    {
        for (uint i = 0; i < kSamplesPerThread; ++i)
        {
            const uint2 grid = uint2(
                groupThreadId.x * kSamplesPerThread + i,
                groupThreadId.y * kSamplesPerThread + j); // 0..31
            const float u = (grid.x + 0.5f) / kDirGridSize;
            const float v = (grid.y + 0.5f) / kDirGridSize;

            const float cosTheta = 1.0f - 2.0f * u;
            const float sinTheta = sqrt(max(0.0f, 1.0f - cosTheta * cosTheta));
            const float phi = 2.0f * ATMOSPHERE_PI * v;

            // ワールド +Y を天頂とする方向ベクトル
            const float3 dir = float3(
                sinTheta * cos(phi),
                cosTheta,
                sinTheta * sin(phi));

            const float3 luminance = SampleSkyLuminance(dir);

            float basis[kCoeffCount];
            EvaluateSHBasis(dir, basis);
            for (uint k = 0; k < kCoeffCount; ++k)
            {
                coeff[k] += luminance * basis[k];
            }
        }
    }

    for (uint c2 = 0; c2 < kCoeffCount; ++c2)
    {
        sCoeffSum[groupIndex][c2] = coeff[c2];
    }
    GroupMemoryBarrierWithGroupSync();

    // groupshared 上の並列リダクション
    for (uint stride = kThreadCount / 2; stride > 0; stride >>= 1)
    {
        if (groupIndex < stride)
        {
            for (uint k = 0; k < kCoeffCount; ++k)
            {
                sCoeffSum[groupIndex][k] += sCoeffSum[groupIndex + stride][k];
            }
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (groupIndex == 0)
    {
        // モンテカルロ正規化: (4π / N) Σ L・Y
        const float sampleWeight = 4.0f * ATMOSPHERE_PI / (kDirGridSize * kDirGridSize);

        // 放射照度畳み込み（Â_l / π）: l=0 → 1, l=1 → 2/3, l=2 → 1/4
        // これを掛けておくことで、評価側は Σ c_i・Y_i(N) だけで「放射照度/π」になる
        const float bandFactor[kCoeffCount] = {
            1.0f,
            2.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f,
            0.25f, 0.25f, 0.25f, 0.25f, 0.25f
        };

        for (uint k = 0; k < kCoeffCount; ++k)
        {
            gSkyIrradianceSH[k] = float4(sCoeffSum[0][k] * sampleWeight * bandFactor[k], 0.0f);
        }
    }
}
