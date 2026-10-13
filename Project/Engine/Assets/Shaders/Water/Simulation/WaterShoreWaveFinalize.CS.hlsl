// ============================================================
// 岸の波の場：(到着時刻, ∂T/∂x, ∂T/∂z, 静水深) にまとめる
// ============================================================

#include "../Common/WaterShoreWave.hlsli"

Texture2D<float> gDepth : register(t0);
Texture2D<float> gArrival : register(t1);
RWTexture2D<float4> gFieldOutput : register(u0);

cbuffer WaterShoreWaveFinalizeConstants : register(b0)
{
    uint gResolution;   // 場の一辺のテクセル数
    float gTexelSize;   // 1 テクセルの幅 [m]
    float2 gShoreWaveFinalizePad;
};

/// @brief 範囲の外は届いていない扱い
float LoadArrival(int2 coord)
{
    if (any(coord < 0) || any(coord >= (int)gResolution))
    {
        return kShoreWaveUnreached;
    }
    return gArrival.Load(int3(coord, 0));
}

/// @brief 片方の軸の到着時刻の傾き（届いていない隣は使わない）
float ArrivalSlope(int2 coord, int2 axis, float center)
{
    const float before = LoadArrival(coord - axis);
    const float after = LoadArrival(coord + axis);
    const bool hasBefore = before < 0.5f * kShoreWaveUnreached;
    const bool hasAfter = after < 0.5f * kShoreWaveUnreached;
    if (hasBefore && hasAfter)
    {
        return (after - before) / (2.0f * gTexelSize);
    }
    if (hasAfter)
    {
        return (after - center) / gTexelSize;
    }
    if (hasBefore)
    {
        return (center - before) / gTexelSize;
    }
    return 0.0f;
}

/// @brief 到着時刻の傾きを求めて場にまとめる。届いていない所は陸として書く
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= gResolution || dispatchThreadId.y >= gResolution)
    {
        return;
    }

    const int2 coord = int2(dispatchThreadId.xy);
    const float arrival = LoadArrival(coord);
    if (arrival >= 0.5f * kShoreWaveUnreached)
    {
        gFieldOutput[coord] = float4(0.0f, 0.0f, 0.0f, kShoreWaveUnreachedDepth);
        return;
    }
    const float depth = gDepth.Load(int3(coord, 0));
    gFieldOutput[coord] = float4(
        arrival,
        ArrivalSlope(coord, int2(1, 0), arrival),
        ArrivalSlope(coord, int2(0, 1), arrival),
        depth);
}
