// ============================================================
// しぶきを動かす：重力と空気の抵抗で進め、水面か地面より下へ落ちた粒を消す。
// 生きている粒の番号を、しぶき・霧ごとの一覧と描く引数へ足す
// ============================================================

#include "WaterSpray.hlsli"
#include "../RayTracing/RTWaterSurfaceCommon.hlsli"

Texture2DArray<float4> gFFTDisplacement : register(t0);
Texture2D<float4> gShoreWaveField : register(t1);
Texture2D<float> gSeabedHeight : register(t2);
RWStructuredBuffer<WaterSprayParticle> gParticles : register(u0);
RWStructuredBuffer<uint> gSprayDrawArgs : register(u1);
RWStructuredBuffer<uint> gSprayDropletList : register(u2);
RWStructuredBuffer<uint> gSprayMistList : register(u3);

cbuffer WaterSprayUpdateConstants : register(b0)
{
    float gUpdateDeltaTime;   // 1 フレームの時間 [s]（0 なら動かさない）
    uint gUpdateCapacity;     // 粒の置き場所の数
    float2 gSeabedOriginXZ;   // 海底の高さの範囲の XZ の最小の角 [m]
    float gSeabedSize;        // 海底の高さの範囲の一辺 [m]（0 なら読まない）
    float3 gUpdatePad;
};

static const float kSprayGravity = 9.81f;
/// @brief しぶきの速さが空気の抵抗で 1 秒に減る割合 [1/s]
static const float kDropletDrag = 0.25f;
/// @brief 霧の速さが空気の抵抗で 1 秒に減る割合 [1/s]
static const float kMistDrag = 1.6f;
/// @brief 霧に掛かる重力の倍率
static const float kMistGravityScale = 0.08f;

/// @brief 位置 worldXZ の海底（陸）の高さ。範囲の外は十分に低い値
float LoadSeabedHeight(float2 worldXZ)
{
    if (gSeabedSize <= 0.0f)
    {
        return -1.0e6f;
    }
    uint width;
    uint height;
    gSeabedHeight.GetDimensions(width, height);
    const float2 uv = (worldXZ - gSeabedOriginXZ) / gSeabedSize;
    if (any(uv < 0.0f) || any(uv >= 1.0f))
    {
        return -1.0e6f;
    }
    const int2 coord = min((int2)(uv * float2(width, height)), int2(width, height) - 1);
    return gSeabedHeight.Load(int3(coord, 0));
}

/// @brief 生きている粒の番号を、種類ごとの一覧と描く引数（1 粒 6 頂点）へ足す
void AppendSprayDraw(uint index, uint kind)
{
    uint vertex;
    if (kind == kSprayKindMist)
    {
        InterlockedAdd(gSprayDrawArgs[kSprayArgsMist], 6u, vertex);
        gSprayMistList[vertex / 6u] = index;
    }
    else
    {
        InterlockedAdd(gSprayDrawArgs[kSprayArgsDroplet], 6u, vertex);
        gSprayDropletList[vertex / 6u] = index;
        InterlockedAdd(gSprayDrawArgs[kSprayArgsDropletCount], 1u);
    }
}

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint index = dispatchThreadId.x;
    if (index >= gUpdateCapacity)
    {
        return;
    }
    WaterSprayParticle p = gParticles[index];
    if (p.lifetime <= 0.0f)
    {
        return;
    }
    p.prevPosition = p.position;
    if (gUpdateDeltaTime <= 0.0f)
    {
        gParticles[index] = p;
        if (IsSprayAlive(p))
        {
            AppendSprayDraw(index, p.kind);
        }
        return;
    }

    p.age += gUpdateDeltaTime;
    if (p.age >= p.lifetime)
    {
        p.lifetime = 0.0f;
        gParticles[index] = p;
        return;
    }

    const bool mist = p.kind == kSprayKindMist;
    p.velocity.y -= kSprayGravity * (mist ? kMistGravityScale : 1.0f) * gUpdateDeltaTime;
    p.velocity *= exp(-(mist ? kMistDrag : kDropletDrag) * gUpdateDeltaTime);
    p.position += p.velocity * gUpdateDeltaTime;

    // 下りている粒が描かれる水面（霧は半径の分だけ下）より下に入ったら消す
    bool dead = false;
    if (p.velocity.y < 0.0f && GetSurfaceShoreWaveParams().enabled)
    {
        const float surfaceY = gSurfaceWaterHeight
            + SampleShoreMeshVertexDisplacementWithShore(gFFTDisplacement, gShoreWaveField, p.position.xz).y;
        dead = p.position.y < surfaceY - (mist ? ComputeSprayDrawRadius(p) : 0.0f);
    }
    // 砂浜や岩より下に入ったら消す
    dead = dead || p.position.y < LoadSeabedHeight(p.position.xz);

    if (dead)
    {
        p.lifetime = 0.0f;
        gParticles[index] = p;
        return;
    }
    gParticles[index] = p;
    AppendSprayDraw(index, p.kind);
}
