// ============================================================
// DXR 海の範囲マップ（水面の範囲全体の水深と上の物までの高さ）
// ============================================================

#include "RTWaterSurfaceCommon.hlsli"
#include "../../Include/RayTracing/RTTrace.hlsli"

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

/// @brief レイの出発点の、水面からの高さ [m]
static const float kStartAboveSurface = 0.02f;
/// @brief 水面の位置が物の中で上の面が見つからないとき、陸の高さとして書く水面からの高さ [m]
static const float kOccupiedHeightAboveSurface = 0.5f;
/// @brief 陸の上の面から上の物を探すレイの出発点の、上の面からの高さ [m]
static const float kStartAboveLandTop = 0.01f;
/// @brief 物の中かを調べる水平のレイのうち、裏から当たればそう見なす本数
static const uint kInsideSolidMinBackHits = 3;

/// @brief 真下か真上へレイを撃ち、最も近い当たりを返す（どの物も不透明として当てる）
RTTraceHit TraceSeaMapRay(float3 origin, float3 direction, float maxDistance)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0f;
    ray.TMax = maxDistance;
    return TraceRTOpaqueClosestHit(gScene, ray, gInstanceMask);
}

/// @brief 点が閉じた物の中か（水平の 4 方向へ撃ったレイが kInsideSolidMinBackHits 本以上、裏から当たる）
bool IsInsideSolid(float3 origin)
{
    const float3 directions[4] =
    {
        float3(1.0f, 0.0f, 0.0f), float3(-1.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, 1.0f), float3(0.0f, 0.0f, -1.0f)
    };
    uint backHits = 0;
    [unroll]
    for (uint i = 0; i < 4; ++i)
    {
        const RTTraceHit hit = TraceSeaMapRay(origin, directions[i], gMaxValue);
        backHits += (hit.hit && !hit.frontFace) ? 1u : 0u;
    }
    return backHits >= kInsideSolidMinBackHits;
}

/// @brief 陸の上の面のすぐ上から真上へレイを撃ち、水面から上の物までの高さ [m] を返す
/// @param origin        波の無い水面のすぐ上の点
/// @param landTopDistance origin から陸の上の面までの距離 [m]
/// @return 表から当たれば当たった面までの高さ、当たらなければ gMaxValue、裏から当たれば（別の物の中）0
float TraceClearanceAboveLand(float3 origin, float landTopDistance)
{
    const float startDistance = landTopDistance + kStartAboveLandTop;
    const RTTraceHit hit = TraceSeaMapRay(
        origin + float3(0.0f, startDistance, 0.0f), float3(0.0f, 1.0f, 0.0f), gUpProbeDistance);
    if (!hit.hit)
    {
        return gMaxValue;
    }
    return hit.frontFace ? min(kStartAboveSurface + startDistance + hit.hitT, gMaxValue) : 0.0f;
}

/// @brief テクセルごとに、波の無い水面のすぐ上から真下と真上へレイを撃ち、(水深, 上の物までの高さ) を書く
/// @details 真下で表から当たった面を海底とする（RTWaterSeabed と同じ決め方）。
///          ただし真上で裏から当たり、水平のレイでも閉じた物の中と分かったときは、海底に刺さった物（柱・壁・岩）の中として陸の下にする。
///          真下で海底が見つからず真上で裏から当たったときも陸の下として、水深に陸の上の面の高さの符号反転を書く。
///          上の物までの高さは、水の上では真上で最初に当たった面（桟橋の床など）までの距離、
///          陸の下では陸の上の面より上で最初に当たった面までの距離（TraceClearanceAboveLand）
[shader("raygeneration")]
void RTWaterSeaMapRayGen()
{
    const uint2 launchIndex = DispatchRaysIndex().xy;
    const float2 worldXZ = gMapOriginXZ + (float2(launchIndex) + 0.5f) * gTexelSize;
    const float3 origin = float3(worldXZ.x, gSurfaceWaterHeight + kStartAboveSurface, worldXZ.y);

    const RTTraceHit down = TraceSeaMapRay(origin, float3(0.0f, -1.0f, 0.0f), gMaxValue + kStartAboveSurface);
    const RTTraceHit up = TraceSeaMapRay(origin, float3(0.0f, 1.0f, 0.0f), gUpProbeDistance);
    const bool closedAbove = up.hit && !up.frontFace;
    const bool seabedBelow = down.hit && down.frontFace && !(closedAbove && IsInsideSolid(origin));

    float depth = gMaxValue;
    float clearance = gMaxValue;
    if (up.hit)
    {
        clearance = min(up.hitT + kStartAboveSurface, gMaxValue);
    }
    if (seabedBelow)
    {
        depth = down.hitT - kStartAboveSurface;
    }
    else if (closedAbove)
    {
        depth = -min(up.hitT + kStartAboveSurface, gMaxValue);
        clearance = TraceClearanceAboveLand(origin, up.hitT);
    }
    else if (down.hit && !down.frontFace)
    {
        depth = -kOccupiedHeightAboveSurface;
        clearance = 0.0f;
    }

    gSeaMapOutput[launchIndex] = float2(depth, clearance);
}
