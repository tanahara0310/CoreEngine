// ============================================================
// 白波の統計
// ============================================================

#include "../Common/WaterWhitecapCoverage.hlsli"

Texture2DArray<float4> gJacobian : register(t0);
Texture2DArray<float> gWhitecapFoam : register(t1);
RWByteAddressBuffer gStatistics : register(u0);
SamplerState gLinearWrap : register(s0);

cbuffer WaterWhitecapStatisticsConstants : register(b0)
{
    float2 gSampleOriginXZ;    // 標本を取る範囲の XZ の最小の角 [m]
    float gSampleSpacing;      // 標本の間隔 [m]
    uint gSampleResolution;    // 標本の格子の一辺の数
    float3 gCascadeWeights;    // カスケード別の勾配の重み
    float gBias;               // 今の砕けるしきい値
    float gGain;               // 今のしきい値からの立ち上がりの傾き
    float gHistogramMin;       // ヒストグラムの detJ の下端
    float gHistogramInvWidth;  // ヒストグラムの 1 段の幅の逆数
    uint gHistogramBins;       // ヒストグラムの段の数
    float3 gWaveGroupPhase;    // 波群エンベロープの位相のずれ [rad]
    float gStatisticsPad;
};

/// @brief 被覆率の和を整数で足すときの倍率
static const float kCoverageFixedPointScale = 16384.0f;

/// @brief 標本 1 点の合成ヤコビアンをヒストグラムへ、白波の被覆率を和へ足す
/// @details 出力は [0, gHistogramBins) が段ごとの数、gHistogramBins 番目が被覆率の和（kCoverageFixedPointScale 倍）
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= gSampleResolution || dispatchThreadId.y >= gSampleResolution)
    {
        return;
    }

    const float2 worldXZ = gSampleOriginXZ + (float2(dispatchThreadId.xy) + 0.5f) * gSampleSpacing;
    const float detJ = ComputeFFTCombinedDetJ(worldXZ, gJacobian, gLinearWrap, gCascadeWeights, gWaveGroupPhase);
    const float coverage = max(
        ComputeWhitecapInstant(detJ, gBias, gGain),
        SampleWhitecapAccumulated(worldXZ, gWhitecapFoam, gLinearWrap, gWaveGroupPhase));

    const uint bin = (uint)clamp((detJ - gHistogramMin) * gHistogramInvWidth, 0.0f, (float)(gHistogramBins - 1u));
    uint original;
    gStatistics.InterlockedAdd(bin * 4u, 1u, original);

    // 被覆率の和はウェーブ内で足してから 1 回だけ書く
    const uint waveCoverage = WaveActiveSum((uint)(coverage * kCoverageFixedPointScale + 0.5f));
    if (WaveIsFirstLane())
    {
        gStatistics.InterlockedAdd(gHistogramBins * 4u, waveCoverage, original);
    }
}
