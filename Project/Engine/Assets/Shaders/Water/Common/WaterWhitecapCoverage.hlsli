// ============================================================
// 白波の被覆率
// ============================================================
#ifndef WATER_WHITECAP_COVERAGE_INCLUDED
#define WATER_WHITECAP_COVERAGE_INCLUDED

#include "FFTOceanCascade.hlsli"

/// @brief 蓄積泡へ掛ける波群エンベロープの写像の係数（envelope² × この係数）
static const float kWhitecapEnvelopeScale = 0.7f;

/// @brief いま砕けている白波の被覆率 [0,1]
/// @param detJ 合成ヤコビアン（ComputeFFTCombinedDetJ）
/// @param bias 砕けるしきい値（detJ がこれを下回ると砕ける）
/// @param gain しきい値からの立ち上がりの傾き
float ComputeWhitecapInstant(float detJ, float bias, float gain)
{
    return saturate((bias - detJ) * gain);
}

/// @brief 砕けた後に残っている白波の被覆率 [0,1]（FFTOceanFoamAccumulate.CS が進めたもの）
/// @details カスケードごとの格子の値の最大に、波群エンベロープを掛けて泡の濃淡を波のセットと揃える
float SampleWhitecapAccumulated(float2 worldXZ, Texture2DArray<float> accumulatedFoam, SamplerState wrapSampler)
{
    float accumulated = 0.0f;
    [unroll]
    for (int ci = 0; ci < kFFTCascadeCount; ++ci)
    {
        const float2 cuv = ComputeFFTCascadeUV(worldXZ, ci);
        accumulated = max(accumulated, accumulatedFoam.SampleLevel(wrapSampler, float3(cuv, (float)ci), 0.0f));
    }
    const float envelope = ComputeFFTWaveGroupEnvelope(worldXZ);
    return saturate(accumulated * envelope * envelope * kWhitecapEnvelopeScale);
}

#endif // WATER_WHITECAP_COVERAGE_INCLUDED
