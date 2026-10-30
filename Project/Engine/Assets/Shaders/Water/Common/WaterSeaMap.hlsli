// ============================================================
// 海の範囲マップ（WaterSeaMapRayTracingManager が水面の範囲全体を測った地図）
// テクセル (i, j) はワールド XZ = 範囲の最小の角 + (i + 0.5, j + 0.5) / texelsPerMeter の点。
// 中身は (水深, 上の物までの高さ, 粗い格子用の水深, 粗い格子用の上の物までの高さ) [m]。
//   - 水深: 測ったときの水面から海底までの深さ。陸の下は負（陸の上の面の高さの符号反転）、
//           海とつながっていない水たまりも負
//   - 上の物までの高さ: 測ったときの水面から、真上で最初に当たる物（桟橋の床など）までの高さ
//   - 粗い格子用: 粗い格子の 1 マスより広い窓の中の最小値（三角形が陸や物の下へまたがらない）
// FFTWater.VS / Water.VS / RTWaterSurfaceCommon.hlsli が同じ式で波の変位を弱める。
// ============================================================
#ifndef WATER_SEA_MAP_HLSLI
#define WATER_SEA_MAP_HLSLI

/// @brief 砕波の限界（波の高さ ≤ この値 × 水深）
static const float kWaterSeaMapBreakingRatio = 0.78f;
/// @brief 上の物までの高さに対して、波の山が届いてよい高さ（波の高さの倍数）
static const float kWaterSeaMapCrestRatio = 1.5f;
/// @brief 範囲の外として返す水深・上の物までの高さ [m]（波を弱めない）
static const float kWaterSeaMapOutsideValue = 64.0f;

/// @brief 地図をワールド XZ でバイリニアに読む（範囲の外は弱めない値を返す）
float4 SampleWaterSeaMap(
    Texture2D<float4> seaMap, float2 worldXZ, float2 originXZ, float texelsPerMeter, uint2 resolution)
{
    const float2 texelPos = (worldXZ - originXZ) * texelsPerMeter - 0.5f;
    const float2 resolutionF = float2(resolution);
    if (any(texelPos < -0.5f) || any(texelPos > resolutionF - 0.5f))
    {
        return kWaterSeaMapOutsideValue.xxxx;
    }
    const int2 maxCoord = int2(resolution) - 1;
    const int2 baseCoord = int2(floor(texelPos));
    const float2 fracCoord = texelPos - float2(baseCoord);
    const int2 p00 = clamp(baseCoord, 0, maxCoord);
    const int2 p11 = clamp(baseCoord + 1, 0, maxCoord);
    const float4 c00 = seaMap.Load(int3(p00.x, p00.y, 0));
    const float4 c10 = seaMap.Load(int3(p11.x, p00.y, 0));
    const float4 c01 = seaMap.Load(int3(p00.x, p11.y, 0));
    const float4 c11 = seaMap.Load(int3(p11.x, p11.y, 0));
    return lerp(lerp(c00, c10, fracCoord.x), lerp(c01, c11, fracCoord.x), fracCoord.y);
}

/// @brief 波の変位に掛ける倍率（0 = 動かさない / 1 = そのまま）
/// @param seaMapValue   SampleWaterSeaMap の値
/// @param conservative  true なら粗い格子用の値を使う
/// @param depthOffset   今の水面の高さ − 測ったときの水面の高さ [m]
/// @param invWaveHeight 波の高さ（有義波高）の逆数 [1/m]
/// @details 波の高さを水深の kWaterSeaMapBreakingRatio 倍までに、波の山を上の物の手前までに抑える
float ComputeWaterSeaMapWaveScale(float4 seaMapValue, bool conservative, float depthOffset, float invWaveHeight)
{
    const float2 depthAndClearance = conservative ? seaMapValue.zw : seaMapValue.xy;
    const float depth = depthAndClearance.x + depthOffset;
    const float clearance = depthAndClearance.y - depthOffset;
    const float byDepth = saturate(kWaterSeaMapBreakingRatio * depth * invWaveHeight);
    const float byClearance = saturate(clearance * invWaveHeight / kWaterSeaMapCrestRatio);
    return min(byDepth, byClearance);
}

/// @brief 頂点の格子の間隔が地図のテクセルより広いか（広ければ粗い格子用の値を使う）
bool UseConservativeWaterSeaMap(float vertexSpacing, float texelsPerMeter)
{
    return vertexSpacing * texelsPerMeter > 1.0f;
}

#endif // WATER_SEA_MAP_HLSLI
