// ============================================================
// 白波の被覆率
// ============================================================
#ifndef WATER_WHITECAP_COVERAGE_INCLUDED
#define WATER_WHITECAP_COVERAGE_INCLUDED

#include "FFTOceanCascade.hlsli"

/// @brief 残りの泡の格子の段の数（0 = カメラの近く・1 = 遠く）
static const uint kWhitecapResidueLevelCount = 2;
/// @brief 格子の端で、外側の値へ移す幅（範囲の一辺に対する割合）
static const float kWhitecapResidueEdgeFade = 0.1f;

/// @brief いま砕けている白波の被覆率 [0,1]
/// @param detJ 合成ヤコビアン（ComputeFFTCombinedDetJ）
/// @param bias 砕けるしきい値（detJ がこれを下回ると砕ける）
/// @param gain しきい値からの立ち上がりの傾き
float ComputeWhitecapInstant(float detJ, float bias, float gain)
{
    return saturate((bias - detJ) * gain);
}

/// @brief 残りの泡の格子の範囲（段ごとの XZ の最小の角と一辺の長さの逆数）
struct WhitecapResidueWindows
{
    float4 originsXZ; ///< xy = 近くの段・zw = 遠くの段の角 [m]（泡を風下へ流した座標）
    float2 invSizes;  ///< x = 近くの段・y = 遠くの段の一辺の長さの逆数 [1/m]
};

/// @brief 格子の 1 段の位置と、範囲の端からの近さを求める
/// @param uv 段の中での位置 [0,1]
/// @return 範囲の端の kWhitecapResidueEdgeFade の幅で 0 から 1 へ上がる値（範囲の外は 0）
float ComputeWhitecapResidueInside(float2 foamXZ, float2 originXZ, float invSize, out float2 uv)
{
    uv = (foamXZ - originXZ) * invSize;
    const float2 edgeDistance = min(uv, 1.0f - uv);
    return saturate(min(edgeDistance.x, edgeDistance.y) / kWhitecapResidueEdgeFade);
}

/// @brief 砕けた後に残っている白波の被覆率 [0,1]（FFTOceanFoamAccumulate.CS が進めたもの）
/// @param baseWorldXZ    変位前の参照格子座標
/// @param residue        残りの泡の格子（段ごとのスライス）
/// @param driftOffsetXZ  泡が風下へ流れた距離 [m]（格子はこの距離だけ流れる座標に置いてある）
/// @param meanCoverage   遠くの段より外で使う、残りの泡の被覆率の平均
/// @details 近くの段の中は近くの段、その外は遠くの段、さらに外は平均を使い、境目は端の幅でなだらかに移す
float SampleWhitecapResidue(
    float2 baseWorldXZ, Texture2DArray<float> residue, SamplerState linearSampler,
    float2 driftOffsetXZ, WhitecapResidueWindows windows, float meanCoverage)
{
    const float2 foamXZ = baseWorldXZ - driftOffsetXZ;

    float2 farUV;
    const float farInside = ComputeWhitecapResidueInside(foamXZ, windows.originsXZ.zw, windows.invSizes.y, farUV);
    float coverage = meanCoverage;
    [branch]
    if (farInside > 0.0f)
    {
        coverage = lerp(meanCoverage, residue.SampleLevel(linearSampler, float3(farUV, 1.0f), 0.0f), farInside);
    }

    float2 nearUV;
    const float nearInside = ComputeWhitecapResidueInside(foamXZ, windows.originsXZ.xy, windows.invSizes.x, nearUV);
    [branch]
    if (nearInside > 0.0f)
    {
        coverage = lerp(coverage, residue.SampleLevel(linearSampler, float3(nearUV, 0.0f), 0.0f), nearInside);
    }
    return saturate(coverage);
}

#endif // WATER_WHITECAP_COVERAGE_INCLUDED
