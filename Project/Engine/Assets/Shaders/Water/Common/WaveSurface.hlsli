// ============================================================
// 波の面の評価（FFT の変位テクスチャをサンプラで読む）
// 水面メッシュの頂点（FFTWater.VS）と岸の泡の時間変化（WaterShoreFoam.CS・WaterShoreSwash.CS）が使う。
// ============================================================
#ifndef WAVE_SURFACE_INCLUDED
#define WAVE_SURFACE_INCLUDED

#include "FFTOceanCascade.hlsli"

/// @brief 頂点の格子の間隔で解像できるカスケードの変位（水平＋高さ）に波群エンベロープを掛けた値
/// @param displacement   カスケードの変位テクスチャ
/// @param wrapSampler    WRAP のサンプラ
/// @param baseXZ         変位前の参照格子座標
/// @param vertexSpacing  頂点の格子の間隔 [m]（ComputeFFTGeometryCascadeWeight）
/// @param waveGroupPhase 波群エンベロープの位相のずれ [rad]
/// @details 各カスケードを回転格子系の UV で読み、水平の変位をワールドへ逆回転して足す
float3 SampleFFTVertexDisplacement(
    Texture2DArray<float4> displacement, SamplerState wrapSampler,
    float2 baseXZ, float vertexSpacing, float3 waveGroupPhase)
{
    float3 sum = float3(0.0f, 0.0f, 0.0f);
    [unroll]
    for (int ci = 0; ci < kFFTCascadeCount; ++ci)
    {
        const float weight = ComputeFFTGeometryCascadeWeight(ci, vertexSpacing);
        if (weight <= 0.0f)
        {
            continue;
        }
        const float2 cuv = ComputeFFTCascadeUV(baseXZ, ci);
        const float3 d = displacement.SampleLevel(wrapSampler, float3(cuv, (float)ci), 0.0f).xyz;
        const float2 horizontal = RotateFromFFTCascadeGrid(float2(d.x, d.z), ci);
        sum += float3(horizontal.x, d.y, horizontal.y) * weight;
    }
    return sum * ComputeFFTWaveGroupEnvelope(baseXZ, waveGroupPhase);
}

/// @brief SampleFFTVertexDisplacement の高さだけ（静水面からの水面の上下 [m]）
float SampleFFTVertexElevation(
    Texture2DArray<float4> displacement, SamplerState wrapSampler,
    float2 baseXZ, float vertexSpacing, float3 waveGroupPhase)
{
    float height = 0.0f;
    [unroll]
    for (int ci = 0; ci < kFFTCascadeCount; ++ci)
    {
        const float weight = ComputeFFTGeometryCascadeWeight(ci, vertexSpacing);
        if (weight <= 0.0f)
        {
            continue;
        }
        const float2 cuv = ComputeFFTCascadeUV(baseXZ, ci);
        height += displacement.SampleLevel(wrapSampler, float3(cuv, (float)ci), 0.0f).y * weight;
    }
    return height * ComputeFFTWaveGroupEnvelope(baseXZ, waveGroupPhase);
}

#endif // WAVE_SURFACE_INCLUDED
