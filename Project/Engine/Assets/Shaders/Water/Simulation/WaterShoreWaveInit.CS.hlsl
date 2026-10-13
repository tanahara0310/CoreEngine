// ============================================================
// 岸の波の場：ならした静水深と、到着時刻の初期値
// ============================================================

#include "../Common/WaterShoreWave.hlsli"

Texture2D<float> gSeabedHeight : register(t0);
Texture2D<float> gPrevArrival : register(t1);
RWTexture2D<float> gDepthOutput : register(u0);
RWTexture2D<float> gArrivalOutput : register(u1);
SamplerState gLinearClamp : register(s0);

cbuffer WaterShoreWaveInitConstants : register(b0)
{
    float2 gWindowOriginXZ;     // 範囲の XZ の最小の角 [m]（海底の高さと同じ範囲）
    float gWindowSize;          // 範囲の一辺 [m]
    uint gResolution;           // 場の一辺のテクセル数
    float2 gPrevWindowOriginXZ; // 前のフレームの範囲の角 [m]
    float gWaterRestHeight;     // 静水面の高さ [m]
    float gStartDepth;          // 波が来始める水深 [m]
    uint gResetArrival;         // 1 = 前のフレームの到着時刻を捨てる
    float2 gDirection;          // うねりの進む向き（XZ・正規化済み）
    float gAngularFrequency;    // ω [rad/s]
};

/// @brief 場の 1 テクセルを覆う海底の高さの平均（2×2 テクセルの角を双線形で読むと 4 テクセルの平均）
float SampleTexelSeabedHeight(float2 centerXZ, float texelSize)
{
    const float quarter = 0.25f * texelSize;
    float sum = 0.0f;
    [unroll]
    for (int j = -1; j <= 1; j += 2)
    {
        [unroll]
        for (int i = -1; i <= 1; i += 2)
        {
            const float2 uv = (centerXZ + float2(i, j) * quarter - gWindowOriginXZ) / gWindowSize;
            sum += gSeabedHeight.SampleLevel(gLinearClamp, uv, 0.0f);
        }
    }
    return 0.25f * sum;
}

/// @brief テクセルごとに、ならした静水深と到着時刻の初期値を書く
/// @details 波の出どころは平面波の時刻。ほかの水は前のフレームの同じ位置の値を引き継ぎ、陸は届かない扱い
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= gResolution || dispatchThreadId.y >= gResolution)
    {
        return;
    }

    const int2 coord = int2(dispatchThreadId.xy);
    const float texelSize = gWindowSize / (float)gResolution;
    const float2 centerXZ = gWindowOriginXZ + (float2(coord) + 0.5f) * texelSize;
    const float depth = gWaterRestHeight - SampleTexelSeabedHeight(centerXZ, texelSize);
    gDepthOutput[coord] = depth;

    float arrival = kShoreWaveUnreached;
    if (IsShoreWaveSource(coord, gResolution, depth, gStartDepth, gDirection))
    {
        arrival = ShoreWaveSourceArrival(centerXZ, gDirection, gAngularFrequency, gStartDepth);
    }
    else if (depth > 0.0f && gResetArrival == 0)
    {
        const int2 previousCoord = (int2)floor((centerXZ - gPrevWindowOriginXZ) / texelSize);
        if (all(previousCoord >= 0) && all(previousCoord < (int)gResolution))
        {
            arrival = gPrevArrival.Load(int3(previousCoord, 0));
        }
    }
    gArrivalOutput[coord] = arrival;
}
