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

/// @brief 泡の乱数に使う 2D ハッシュ（[0,1)）
/// @param p 格子点（整数値の float2）
/// @details 格子点の整数座標から整数演算で作るので、原点から遠くても値の分布が変わらない。
///          Tools/Water/generate_foam_coverage_table.py が同じ式を使う
float FoamHash(float2 p)
{
    const uint2 q = asuint(int2(p));
    uint h = (q.x * 0x8da6b343u) ^ (q.y * 0xd8163841u);
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return (float)(h >> 8) * (1.0f / 16777216.0f);
}

/// @brief 泡の value noise（[0,1]・C1 連続）
float FoamValueNoise(float2 p)
{
    const float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0f - 2.0f * f);
    const float a = FoamHash(i);
    const float b = FoamHash(i + float2(1.0f, 0.0f));
    const float c = FoamHash(i + float2(0.0f, 1.0f));
    const float d = FoamHash(i + float2(1.0f, 1.0f));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

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

/// @brief 砕けた後に残っている白波（FFTOceanFoamAccumulate.CS が進めたもの）
/// @param baseWorldXZ    変位前の参照格子座標
/// @param residue        残りの泡の格子（段ごとのスライス。x = 被覆率・y = 砕けた直後の厚い泡の量）
/// @param driftOffsetXZ  泡が風下へ流れた距離 [m]（格子はこの距離だけ流れる座標に置いてある）
/// @param meanCoverage   遠くの段より外で使う、残りの泡の被覆率の平均
/// @return x = 被覆率 [0,1]・y = そのうち砕けた直後の厚い泡の量 [0,x]
/// @details 近くの段の中は近くの段、その外は遠くの段、さらに外は平均（厚い泡は 0）を使い、
///          境目は端の幅でなだらかに移す
float2 SampleWhitecapResidue(
    float2 baseWorldXZ, Texture2DArray<float2> residue, SamplerState linearSampler,
    float2 driftOffsetXZ, WhitecapResidueWindows windows, float meanCoverage)
{
    const float2 foamXZ = baseWorldXZ - driftOffsetXZ;

    float2 farUV;
    const float farInside = ComputeWhitecapResidueInside(foamXZ, windows.originsXZ.zw, windows.invSizes.y, farUV);
    float2 foam = float2(meanCoverage, 0.0f);
    [branch]
    if (farInside > 0.0f)
    {
        foam = lerp(foam, residue.SampleLevel(linearSampler, float3(farUV, 1.0f), 0.0f), farInside);
    }

    float2 nearUV;
    const float nearInside = ComputeWhitecapResidueInside(foamXZ, windows.originsXZ.xy, windows.invSizes.x, nearUV);
    [branch]
    if (nearInside > 0.0f)
    {
        foam = lerp(foam, residue.SampleLevel(linearSampler, float3(nearUV, 0.0f), 0.0f), nearInside);
    }
    return saturate(foam);
}

#endif // WATER_WHITECAP_COVERAGE_INCLUDED
