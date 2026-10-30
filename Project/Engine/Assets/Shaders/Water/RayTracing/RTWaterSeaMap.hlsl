// ============================================================
// DXR 海の範囲マップ（水面の範囲全体の水深と上の物までの高さ）
// ============================================================

#include "RTWaterSurfaceCommon.hlsli"

RWTexture2D<float2> gSeaMapOutput : register(u0);
RaytracingAccelerationStructure gScene : register(t0);

cbuffer WaterSeaMapConstants : register(b0)
{
    float2 gMapOriginXZ;    // 範囲の XZ の最小の角 [m]
    float gTexelSize;       // 1 テクセルの幅 [m]
    float gMaxValue;        // 水深・上の物までの高さの上限 [m]
    float gUpProbeDistance; // 上向きのレイの長さ [m]
    uint gInstanceMask;     // 当てるインスタンスの印
    float2 gSeaMapPad;
};

/// @brief 海の範囲マップのレイのペイロード
struct RTSeaMapPayload
{
    float hitT;
    float hitKind; // 0 = 当たらない / 1 = 表から当たった / 2 = 裏から当たった
};

#ifdef __INTELLISENSE__
#define RAY_FLAG_NONE 0x0
void TraceRay(
    RaytracingAccelerationStructure scene,
    uint rayFlags,
    uint instanceInclusionMask,
    uint rayContributionToHitGroupIndex,
    uint multiplierForGeometryContributionToHitGroupIndex,
    uint missShaderIndex,
    RayDesc ray,
    inout RTSeaMapPayload payload);
#endif

/// @brief レイの出発点の、水面からの高さ [m]
static const float kStartAboveSurface = 0.02f;
/// @brief 水面の位置が物の中で上の面が見つからないとき、陸の高さとして書く水面からの高さ [m]
static const float kOccupiedHeightAboveSurface = 0.5f;

RTSeaMapPayload TraceSeaMapRay(float3 origin, float3 direction, float maxDistance)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0f;
    ray.TMax = maxDistance;

    RTSeaMapPayload payload;
    payload.hitT = 0.0f;
    payload.hitKind = 0.0f;
    TraceRay(gScene, RAY_FLAG_NONE, gInstanceMask, 0, 1, 0, ray, payload);
    return payload;
}

/// @brief テクセルごとに、波の無い水面のすぐ上から真下と真上へレイを撃ち、(水深, 上の物までの高さ) を書く
/// @details 真下で表から当たった面を海底とする（RTWaterSeabed と同じ決め方）。
///          真下で海底が見つからず真上で裏から当たったときは陸の下として、水深に陸の上の面の高さの符号反転を書く。
///          上の物までの高さは、真上で最初に当たった面（桟橋の床や陸の上の面）までの距離
[shader("raygeneration")]
void RTWaterSeaMapRayGen()
{
    const uint2 launchIndex = DispatchRaysIndex().xy;
    const float2 worldXZ = gMapOriginXZ + (float2(launchIndex) + 0.5f) * gTexelSize;
    const float3 origin = float3(worldXZ.x, gSurfaceWaterHeight + kStartAboveSurface, worldXZ.y);

    const RTSeaMapPayload down = TraceSeaMapRay(origin, float3(0.0f, -1.0f, 0.0f), gMaxValue + kStartAboveSurface);
    const RTSeaMapPayload up = TraceSeaMapRay(origin, float3(0.0f, 1.0f, 0.0f), gUpProbeDistance);

    float depth = gMaxValue;
    float clearance = gMaxValue;
    if (up.hitKind != 0.0f)
    {
        clearance = min(up.hitT + kStartAboveSurface, gMaxValue);
    }
    if (down.hitKind == 1.0f)
    {
        depth = down.hitT - kStartAboveSurface;
    }
    else if (up.hitKind == 2.0f)
    {
        depth = -min(up.hitT + kStartAboveSurface, gMaxValue);
        clearance = 0.0f;
    }
    else if (down.hitKind == 2.0f)
    {
        depth = -kOccupiedHeightAboveSurface;
        clearance = 0.0f;
    }

    gSeaMapOutput[launchIndex] = float2(depth, clearance);
}

[shader("miss")]
void RTWaterSeaMapMiss(inout RTSeaMapPayload payload)
{
    payload.hitT = 0.0f;
    payload.hitKind = 0.0f;
}

[shader("closesthit")]
void RTWaterSeaMapClosestHit(
    inout RTSeaMapPayload payload,
    in BuiltInTriangleIntersectionAttributes attr)
{
    payload.hitT = RayTCurrent();
    payload.hitKind = (HitKind() == HIT_KIND_TRIANGLE_FRONT_FACE) ? 1.0f : 2.0f;
}
