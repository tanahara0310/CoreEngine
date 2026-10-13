// ============================================================
// 岸の波の場：到着時刻を 1 回進める
// ============================================================

#include "../Common/WaterShoreWave.hlsli"

Texture2D<float> gDepth : register(t0);
Texture2D<float> gArrivalIn : register(t1);
RWTexture2D<float> gArrivalOutput : register(u0);

cbuffer WaterShoreWaveSolveConstants : register(b0)
{
    uint gResolution;          // 場の一辺のテクセル数
    float gTexelSize;          // 1 テクセルの幅 [m]
    float gAngularFrequency;   // ω [rad/s]
    float gStartDepth;         // 波が来始める水深 [m]
    float2 gWindowOriginXZ;    // 範囲の XZ の最小の角 [m]
    float2 gDirection;         // うねりの進む向き（XZ・正規化済み）
};

/// @brief 水の隣の到着時刻（範囲の外と陸は届いていない扱い）
float LoadWaterArrival(int2 coord)
{
    if (any(coord < 0) || any(coord >= (int)gResolution) || gDepth.Load(int3(coord, 0)) <= 0.0f)
    {
        return kShoreWaveUnreached;
    }
    return gArrivalIn.Load(int3(coord, 0));
}

/// @brief 水の隣の到着時刻から、|∇T| = 1 / 速さ を満たす到着時刻を求める（上流側の差分）
/// @details 出どころは平面波の時刻のまま。陸は波を通さない
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= gResolution || dispatchThreadId.y >= gResolution)
    {
        return;
    }

    const int2 coord = int2(dispatchThreadId.xy);
    const float depth = gDepth.Load(int3(coord, 0));
    if (depth <= 0.0f)
    {
        gArrivalOutput[coord] = kShoreWaveUnreached;
        return;
    }
    if (IsShoreWaveSource(coord, gResolution, depth, gStartDepth, gDirection))
    {
        const float2 centerXZ = gWindowOriginXZ + (float2(coord) + 0.5f) * gTexelSize;
        gArrivalOutput[coord] = ShoreWaveSourceArrival(centerXZ, gDirection, gAngularFrequency, gStartDepth);
        return;
    }

    const float a = min(LoadWaterArrival(coord + int2(-1, 0)), LoadWaterArrival(coord + int2(1, 0)));
    const float b = min(LoadWaterArrival(coord + int2(0, -1)), LoadWaterArrival(coord + int2(0, 1)));
    if (min(a, b) >= 0.5f * kShoreWaveUnreached)
    {
        gArrivalOutput[coord] = kShoreWaveUnreached;
        return;
    }
    // 片方の軸に届いた隣が無いときは、もう片方の軸だけで進める
    const float step = gTexelSize / ShoreWavePhaseSpeed(gAngularFrequency, depth);
    float arrival;
    if (abs(a - b) >= step)
    {
        arrival = min(a, b) + step;
    }
    else
    {
        arrival = 0.5f * (a + b + sqrt(2.0f * step * step - (a - b) * (a - b)));
    }
    gArrivalOutput[coord] = arrival;
}
