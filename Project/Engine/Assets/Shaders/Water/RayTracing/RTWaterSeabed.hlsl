// ============================================================
// DXR 海底の高さ
// ============================================================

#include "RTWaterSurfaceCommon.hlsli"
#include "../../Include/RayTracing/RTTrace.hlsli"

RWTexture2D<float> gSeabedHeightOutput : register(u0);
RaytracingAccelerationStructure gScene : register(t0);
Texture2DArray<float4> gFFTOceanDisplacement : register(t1);

cbuffer WaterSeabedConstants : register(b0)
{
    float2 gWindowOriginXZ; // 範囲の XZ の最小の角 [m]
    float gTexelSize;       // 1 テクセルの幅 [m]
    float gMaxDepth;        // 海底を探す水面からの深さの上限 [m]
    uint gInstanceMask;     // 海底として当てるインスタンスの印
    float3 gSeabedPad;
};

/// @brief レイの出発点の、水面からの高さ [m]
static const float kStartAboveSurface = 0.02f;
/// @brief 水面の位置が物の中で上の面が見つからないとき、海底の代わりに書く水面からの高さ [m]
static const float kOccupiedHeightAboveSurface = 0.5f;
/// @brief 陸や物の上の面を探す上向きのレイの長さ [m]
static const float kDryLandProbeDistance = 20.0f;

/// @brief 真下か真上へレイを撃ち、最も近い当たりを返す（どの物も不透明として当てる）
RTTraceHit TraceSeabedRay(float3 origin, float3 direction, float maxDistance)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0f;
    ray.TMax = maxDistance;
    return TraceRTOpaqueClosestHit(gScene, ray, gInstanceMask);
}

/// @brief テクセルごとに、今の水面のすぐ上から真下へレイを撃ち、水面より下で最初に当たる面
///        （海底・水中の物）の高さを書く
/// @details 水面より上の物（桟橋の床など）は出発点より上にあるので当たらない。
///          水面の点が陸の下や物の中のときは、上向きのレイで陸や物の上の面の高さを書く（水深が負になる）
[shader("raygeneration")]
void RTWaterSeabedRayGen()
{
    const uint2 launchIndex = DispatchRaysIndex().xy;
    const float2 worldXZ = gWindowOriginXZ + (float2(launchIndex) + 0.5f) * gTexelSize;
    const float waterY = gSurfaceWaterHeight + EvaluateDrawnSurfaceHeight(gFFTOceanDisplacement, worldXZ);
    const float3 origin = float3(worldXZ.x, waterY + kStartAboveSurface, worldXZ.y);

    // 範囲内に何も無ければ深い海
    float seabedY = waterY - gMaxDepth;

    const RTTraceHit down = TraceSeabedRay(origin, float3(0.0f, -1.0f, 0.0f), gMaxDepth + kStartAboveSurface);
    if (down.hit && down.frontFace)
    {
        seabedY = origin.y - down.hitT;
    }
    else
    {
        // 物の中（下へ撃って裏に当たった）か、下に何も無く上に地面の裏がある（陸の下）とき、
        // 上の面の高さを書く
        const RTTraceHit up = TraceSeabedRay(origin, float3(0.0f, 1.0f, 0.0f), kDryLandProbeDistance);
        if (up.hit && !up.frontFace)
        {
            seabedY = origin.y + up.hitT;
        }
        else if (down.hit && !down.frontFace)
        {
            seabedY = waterY + kOccupiedHeightAboveSurface;
        }
    }

    gSeabedHeightOutput[launchIndex] = seabedY;
}
