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
    float2 gSampleOriginXZ;       // 標本を取る範囲の XZ の最小の角 [m]（参照格子座標）
    float gSampleSpacing;         // 標本の間隔 [m]
    uint gSampleResolution;       // 標本の格子の一辺の数
    float3 gCascadeWeights;       // カスケード別の勾配の重み
    float gBias;                  // 今の砕けるしきい値
    float gGain;                  // 今のしきい値からの立ち上がりの傾き
    float gHistogramMin;          // ヒストグラムの detJ の下端
    float gHistogramInvWidth;     // ヒストグラムの 1 段の幅の逆数
    uint gHistogramBins;          // ヒストグラムの段の数
    float3 gWaveGroupPhase;       // 波群エンベロープの位相のずれ [rad]
    float gResidueMeanCoverage;   // 残りの泡の格子より外で使う被覆率の平均
    float4 gResidueOriginsXZ;     // 残りの泡の格子の段ごとの角 [m]（xy = 近く・zw = 遠く）
    float2 gResidueInvSizes;      // 残りの泡の格子の段ごとの一辺の長さの逆数 [1/m]
    float2 gDriftOffsetXZ;        // 白波の泡が風下へ流れた距離 [m]
};

/// @brief 被覆率の和を整数で足すときの倍率
static const float kCoverageFixedPointScale = 16384.0f;

/// @brief ウェーブ内で足した被覆率を、統計のバッファの wordIndex 番目の語へ 1 回だけ足す
void AccumulateCoverage(uint wordIndex, float coverage)
{
    const uint waveCoverage = WaveActiveSum((uint)(coverage * kCoverageFixedPointScale + 0.5f));
    if (WaveIsFirstLane())
    {
        uint original;
        gStatistics.InterlockedAdd(wordIndex * 4u, waveCoverage, original);
    }
}

/// @brief 標本 1 点の合成ヤコビアンをヒストグラムへ、白波の被覆率を和へ足す
/// @details 出力は [0, gHistogramBins) が段ごとの数、gHistogramBins 番目が白波の被覆率の和、
///          その次が砕けている最中の被覆率の和、その次が残りの泡の被覆率の和（どれも kCoverageFixedPointScale 倍）
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= gSampleResolution || dispatchThreadId.y >= gSampleResolution)
    {
        return;
    }

    const float2 worldXZ = gSampleOriginXZ + (float2(dispatchThreadId.xy) + 0.5f) * gSampleSpacing;
    const float detJ = ComputeFFTCombinedDetJ(worldXZ, gJacobian, gLinearWrap, gCascadeWeights, gWaveGroupPhase);
    const float instant = ComputeWhitecapInstant(detJ, gBias, gGain);

    WhitecapResidueWindows windows;
    windows.originsXZ = gResidueOriginsXZ;
    windows.invSizes = gResidueInvSizes;
    const float residue = SampleWhitecapResidue(
        worldXZ, gWhitecapFoam, gLinearWrap, gDriftOffsetXZ, windows, gResidueMeanCoverage);

    const uint bin = (uint)clamp((detJ - gHistogramMin) * gHistogramInvWidth, 0.0f, (float)(gHistogramBins - 1u));
    uint original;
    gStatistics.InterlockedAdd(bin * 4u, 1u, original);

    AccumulateCoverage(gHistogramBins, max(instant, residue));
    AccumulateCoverage(gHistogramBins + 1u, instant);
    AccumulateCoverage(gHistogramBins + 2u, residue);
}
