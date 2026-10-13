// ============================================================
// しぶきを出す：岸の波が砕けている峰から粒を出す
// ------------------------------------------------------------
// 範囲（岸の波の場と同じ）を 1 スレッド 1 マスで受け持ち、マスの中の乱れた位置で岸の波を求める。
// 砕けている度合いが大きく、峰の上か前の面にある所ほど多く出す。
// しぶきは近い位置・近い速さの粒の塊（水の筋）で出し、塊ごとに波の速さで前へ、
// 砕けた波高に応じた高さまで上へ飛ばす。霧は 1 粒ずつ出す。
// 粒の置き場所は輪のように使い、満杯なら古い粒から上書きする
// ============================================================

#include "WaterSpray.hlsli"
#include "../RayTracing/RTWaterSurfaceCommon.hlsli"

Texture2DArray<float4> gFFTDisplacement : register(t0);
Texture2D<float4> gShoreWaveField : register(t1);
RWStructuredBuffer<WaterSprayParticle> gParticles : register(u0);
RWStructuredBuffer<uint> gSprayCounter : register(u1);

cbuffer WaterSprayEmitConstants : register(b0)
{
    float2 gEmitOriginXZ;   // 範囲の XZ の最小の角 [m]
    float gEmitCellSize;    // 1 マスの幅 [m]
    uint gEmitResolution;   // 範囲の一辺のマス数
    float gEmitDeltaTime;   // 1 フレームの時間 [s]
    float gSprayAmount;     // 出す量の倍率
    uint gEmitFrameIndex;   // 乱数の種
    uint gSprayCapacity;    // 粒の置き場所の数
    float gDropletRadius;   // しぶきの半径 [m]
    float gMistRadius;      // 霧の半径 [m]
    float2 gEmitPad;
};

/// @brief 砕けた波高 1m・峰の上の 1m² から 1 秒に出すしぶきの塊の数
static const float kSprayClusterRate = 10.0f;
/// @brief しぶきの塊 1 つの粒の数
static const uint kSprayClusterSize = 16u;
/// @brief 同じく 1 秒に出す霧の数
static const float kSprayMistRate = 4.0f;
/// @brief 峰の上（後ろ側）で出す幅（1 周期に対する割合）
static const float kSprayCrestBand = 0.05f;
/// @brief 峰の前の面で出す幅（1 周期に対する割合）
static const float kSprayFaceBand = 0.015f;
/// @brief 1 マスから 1 フレームに出す塊の数の上限
static const uint kSprayMaxClustersPerCell = 2u;
/// @brief 1 マスから 1 フレームに出す霧の数の上限
static const uint kSprayMaxMistPerCell = 2u;
/// @brief 砕けた波高に対する、しぶきが上がる高さの割合
static const float kSprayLiftScale = 1.0f;
/// @brief しぶきを出し始める、砕けている度合い
static const float kSprayBreakingStart = 0.5f;
/// @brief 塊の中の粒の速さのばらつき（塊の速さに対する割合）
static const float kSprayClusterVelocitySpread = 0.12f;
static const float kSprayGravity = 9.81f;
/// @brief しぶきを出す最小の水深 [m]
static const float kSprayMinDepth = 0.05f;

/// @brief -1〜1 の乱数を 3 つ
float3 SprayRandomSigned3(inout uint rng)
{
    return float3(SprayRandom(rng), SprayRandom(rng), SprayRandom(rng)) * 2.0f - 1.0f;
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= gEmitResolution || dispatchThreadId.y >= gEmitResolution || gEmitDeltaTime <= 0.0f)
    {
        return;
    }
    const ShoreWaveParams params = GetSurfaceShoreWaveParams();
    if (!params.enabled)
    {
        return;
    }

    uint rng = SprayHash(dispatchThreadId.x ^ SprayHash(dispatchThreadId.y ^ SprayHash(gEmitFrameIndex)));
    const float2 jitter = float2(SprayRandom(rng), SprayRandom(rng));
    const float2 baseXZ = gEmitOriginXZ + (float2(dispatchThreadId.xy) + jitter) * gEmitCellSize;

    const ShoreWaveResult shore = EvaluateShoreWave(gShoreWaveField, params, baseXZ, gSurfaceShoreWavePhaseTime);
    if (shore.breaking <= 0.0f || shore.depth <= kSprayMinDepth || shore.speed <= 0.0f)
    {
        return;
    }

    // 峰の上（位相 0 の後ろ側）と前の面（位相 1 の手前）だけから出す
    const float signedPhase = shore.phase < 0.5f ? shore.phase : shore.phase - 1.0f;
    const float band = signedPhase >= 0.0f
        ? exp(-signedPhase / kSprayCrestBand)
        : exp(signedPhase / kSprayFaceBand);

    // 出す数は砕けている度合い（kSprayBreakingStart から上）・波高・峰からの近さに比例する
    const float strength = saturate((shore.breaking - kSprayBreakingStart) / (1.0f - kSprayBreakingStart))
        * shore.height * band;
    const float scale = gSprayAmount * strength * gEmitCellSize * gEmitCellSize * gEmitDeltaTime;
    const uint clusterCount = min((uint)floor(scale * kSprayClusterRate + SprayRandom(rng)), kSprayMaxClustersPerCell);
    const uint mistCount = min((uint)floor(scale * kSprayMistRate + SprayRandom(rng)), kSprayMaxMistPerCell);
    const uint dropletCount = clusterCount * kSprayClusterSize;
    const uint total = dropletCount + mistCount;
    if (total == 0u)
    {
        return;
    }

    // 描かれる水面の点（FFT の波と岸の波のずれを足した位置）
    const float3 offset = SampleShoreMeshVertexDisplacementWithShore(gFFTDisplacement, gShoreWaveField, baseXZ);
    const float3 origin = float3(baseXZ.x + offset.x, gSurfaceWaterHeight + offset.y, baseXZ.y + offset.z);
    const float3 forward = float3(shore.direction.x, 0.0f, shore.direction.y);
    const float3 tangent = float3(-shore.direction.y, 0.0f, shore.direction.x);

    uint first;
    InterlockedAdd(gSprayCounter[0], total, first);

    // ---- しぶきの塊 ----
    uint slot = first;
    for (uint c = 0u; c < clusterCount; ++c)
    {
        // 塊の速さ：前へは波の速さ前後、横へは少しばらす。上へは決めた高さまで上がる速さ
        const float forwardSpeed = shore.speed * lerp(0.45f, 1.15f, SprayRandom(rng));
        const float lateralSpeed = (2.0f * SprayRandom(rng) - 1.0f) * 0.2f * shore.speed;
        const float liftRandom = SprayRandom(rng);
        const float lift = shore.height * kSprayLiftScale * lerp(0.15f, 1.0f, liftRandom * liftRandom);
        const float3 clusterVelocity = forward * forwardSpeed + tangent * lateralSpeed
            + float3(0.0f, sqrt(2.0f * kSprayGravity * lift), 0.0f);
        const float3 clusterCenter = origin
            + tangent * ((SprayRandom(rng) - 0.5f) * gEmitCellSize)
            + forward * ((SprayRandom(rng) - 0.5f) * gEmitCellSize)
            + float3(0.0f, SprayRandom(rng) * 0.15f * shore.height, 0.0f);
        const float clusterRadius = gDropletRadius * lerp(2.0f, 4.0f, SprayRandom(rng));
        const float lifetime = lerp(1.5f, 2.5f, SprayRandom(rng));
        const float spreadSpeed = kSprayClusterVelocitySpread * length(clusterVelocity);

        for (uint k = 0u; k < kSprayClusterSize; ++k)
        {
            WaterSprayParticle p;
            p.kind = kSprayKindDroplet;
            p.position = clusterCenter + SprayRandomSigned3(rng) * clusterRadius;
            p.prevPosition = p.position;
            p.velocity = clusterVelocity + SprayRandomSigned3(rng) * spreadSpeed;
            p.radius = gDropletRadius * lerp(0.5f, 1.0f, SprayRandom(rng));
            p.lifetime = lifetime * lerp(0.85f, 1.0f, SprayRandom(rng));
            p.age = 0.0f;
            p.seed = SprayRandom(rng);
            p.pad = float2(0.0f, 0.0f);
            gParticles[slot % gSprayCapacity] = p;
            ++slot;
        }
    }

    // ---- 霧 ----
    for (uint m = 0u; m < mistCount; ++m)
    {
        const float forwardSpeed = shore.speed * lerp(0.3f, 0.7f, SprayRandom(rng));
        const float lift = shore.height * kSprayLiftScale * lerp(0.3f, 1.0f, SprayRandom(rng));
        WaterSprayParticle p;
        p.kind = kSprayKindMist;
        p.position = origin + tangent * ((SprayRandom(rng) - 0.5f) * gEmitCellSize)
            + float3(0.0f, SprayRandom(rng) * 0.3f * shore.height, 0.0f);
        p.prevPosition = p.position;
        p.velocity = forward * forwardSpeed + float3(0.0f, 0.6f * sqrt(2.0f * kSprayGravity * lift), 0.0f);
        p.radius = gMistRadius * lerp(0.6f, 1.4f, SprayRandom(rng));
        p.lifetime = lerp(1.5f, 3.0f, SprayRandom(rng));
        p.age = 0.0f;
        p.seed = SprayRandom(rng);
        p.pad = float2(0.0f, 0.0f);
        gParticles[slot % gSprayCapacity] = p;
        ++slot;
    }
}
