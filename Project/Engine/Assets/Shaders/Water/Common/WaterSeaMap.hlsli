// ============================================================
// 海の範囲マップ（WaterSeaMapRayTracingManager が水面の範囲全体を測った地図）
// テクセル (i, j) はワールド XZ = 範囲の最小の角 + (i + 0.5, j + 0.5) / texelsPerMeter の点。
// 中身は (水深, 上の物までの高さ, 粗い格子用の水深, 粗い格子用の上の物までの高さ) [m]。
//   - 水深: 測ったときの水面から海底までの深さ。陸の下は負（陸の上の面の高さの符号反転・-63 まで）、
//           海とつながっていない水たまりは -64
//   - 上の物までの高さ: 測ったときの水面から、真上で最初に当たる物（桟橋の床など）までの高さ。
//                       陸の下は、陸の上の面より上で最初に当たる物までの高さ
//   - 粗い格子用: 粗い格子の 1 マスより広い窓の中の最小値（三角形が陸や物の下へまたがらない。上の物までの高さは陸の下を 0 として取る）
// テクスチャの行 [Z のテクセル数, 2 × Z のテクセル数) は岸へ寄せる波が沖から着くまでの時間の場（SampleShoreWaveField）。
// FFTWater.VS / Water.VS / RTWaterSurfaceCommon.hlsli が同じ式で波の変位を弱める。
// ============================================================
#ifndef WATER_SEA_MAP_HLSLI
#define WATER_SEA_MAP_HLSLI

#include "WaterPhysics.hlsli"

/// @brief 上の物までの高さに対して、波の山が届いてよい高さ（波の高さの倍数）
static const float kWaterSeaMapCrestRatio = 1.5f;
/// @brief 範囲の外として返す水深・上の物までの高さ [m]（波を弱めない）
static const float kWaterSeaMapOutsideValue = 64.0f;
/// @brief 海とつながっていない水たまりの水深 [m]（WaterSeaMapBuilder::kBasinDepth と同じ値）
static const float kWaterSeaMapBasinDepth = -64.0f;

/// @brief 地図の水深（x）が、海とつながっていない水たまりのテクセルか
bool IsWaterSeaMapBasin(float depth)
{
    return depth < kWaterSeaMapBasinDepth + 0.5f;
}

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

/// @brief 地図に書いた、沖から着くまでの時間を割った余りの周期 [s]（WaterSeaMapBuilder::kShoreWaveCycleSeconds）
static const float kShoreWaveFieldCycleSeconds = 144.0f;

/// @brief 沖から着くまでの時間の差を、周期で折り返して [-周期/2, 周期/2) へ入れる
float WrapShoreWaveFieldSeconds(float difference)
{
    return difference - kShoreWaveFieldCycleSeconds * floor(difference / kShoreWaveFieldCycleSeconds + 0.5f);
}

/// @brief 岸へ寄せる波の、沖から着くまでの時間の場をワールド XZ でバイリニアに読む
/// @return (沖から着くまでの時間 [s]（kShoreWaveFieldCycleSeconds の倍数の分だけずれてよい）,
///         その X の傾き, Z の傾き [s/m], 波を運んできた沖の点の峰に沿った位置 [m])。範囲の外は 0
float4 SampleShoreWaveField(
    Texture2D<float4> seaMap, float2 worldXZ, float2 originXZ, float texelsPerMeter, uint2 resolution)
{
    const float2 texelPos = (worldXZ - originXZ) * texelsPerMeter - 0.5f;
    const float2 resolutionF = float2(resolution);
    if (any(texelPos < -0.5f) || any(texelPos > resolutionF - 0.5f))
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    const int2 maxCoord = int2(resolution) - 1;
    const int2 baseCoord = int2(floor(texelPos));
    const float2 fracCoord = texelPos - float2(baseCoord);
    const int2 p00 = clamp(baseCoord, 0, maxCoord);
    const int2 p11 = clamp(baseCoord + 1, 0, maxCoord);
    const int rowOffset = int(resolution.y);
    const float4 c00 = seaMap.Load(int3(p00.x, p00.y + rowOffset, 0));
    float4 c10 = seaMap.Load(int3(p11.x, p00.y + rowOffset, 0));
    float4 c01 = seaMap.Load(int3(p00.x, p11.y + rowOffset, 0));
    float4 c11 = seaMap.Load(int3(p11.x, p11.y + rowOffset, 0));
    // 割った余りの境目をまたいでも時間がつながるよう、c00 の時間に近い側へ折り返してから混ぜる
    c10.x = c00.x + WrapShoreWaveFieldSeconds(c10.x - c00.x);
    c01.x = c00.x + WrapShoreWaveFieldSeconds(c01.x - c00.x);
    c11.x = c00.x + WrapShoreWaveFieldSeconds(c11.x - c00.x);
    return lerp(lerp(c00, c10, fracCoord.x), lerp(c01, c11, fracCoord.x), fracCoord.y);
}

/// @brief 波の変位に掛ける倍率（0 = 動かさない / 1 = そのまま）
/// @param seaMapValue   SampleWaterSeaMap の値
/// @param conservative  true なら粗い格子用の値を使う
/// @param depthOffset   今の水面の高さ − 測ったときの水面の高さ [m]
/// @param invWaveHeight 波の高さ（有義波高）の逆数 [1/m]
/// @details 波の高さを水深の kWaterBreakingIndex 倍までに、波の山を上の物の手前までに抑える
float ComputeWaterSeaMapWaveScale(float4 seaMapValue, bool conservative, float depthOffset, float invWaveHeight)
{
    const float2 depthAndClearance = conservative ? seaMapValue.zw : seaMapValue.xy;
    const float depth = depthAndClearance.x + depthOffset;
    const float clearance = depthAndClearance.y - depthOffset;
    const float byDepth = saturate(kWaterBreakingIndex * depth * invWaveHeight);
    const float byClearance = saturate(clearance * invWaveHeight / kWaterSeaMapCrestRatio);
    return min(byDepth, byClearance);
}

/// @brief 頂点の格子の間隔が地図のテクセルより広いか（広ければ粗い格子用の値を使う）
bool UseConservativeWaterSeaMap(float vertexSpacing, float texelsPerMeter)
{
    return vertexSpacing * texelsPerMeter > 1.0f;
}

#endif // WATER_SEA_MAP_HLSLI
