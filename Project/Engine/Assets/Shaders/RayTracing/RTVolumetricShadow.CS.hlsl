// ============================================================
// 太陽から見た深さの画像（ボリュメトリックフォグの物の影）
// 光に垂直な面に並べた点から光の進む向きへレイを撃ち、最初に当たった距離を書く。
// 当たらなかった所は kRTVolumetricShadowMiss。霧の格子が点の距離と比べて影を決める。
// ============================================================

#include "../Include/RayTracing/RTTrace.hlsli"

cbuffer RTVolumetricShadowConstants : register(b0)
{
    float3 gOrigin;              // 面の中心（レイの始点の面）[m]
    float gSize;                 // 面の一辺 [m]
    float3 gRight;               // 面の右の向き
    float gRange;                // レイの長さ [m]
    float3 gUp;                  // 面の上の向き
    uint gResolution;            // 画像の一辺の画素数
    float3 gDirection;           // レイの向き（光の進行方向）
    float gTexelSize;            // 1 画素の幅 [m]
    uint gHitInstanceTableIndex; // 材質の表（kRTHitNoTexture = アルファを調べず、どの物も不透明として当てる）
    uint gHitSubMeshTableIndex;
    uint gPad0_;
    uint gPad1_;
};

RaytracingAccelerationStructure gScene : register(t0);
RWTexture2D<float> gSunDepth : register(u0);

/// @brief 当たらなかった画素に書く距離
static const float kRTVolumetricShadowMiss = 1.0e30f;

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 texel = dispatchThreadId.xy;
    if (any(texel >= gResolution))
    {
        return;
    }

    const float2 uv = (float2(texel) + 0.5f) / float(gResolution);

    RayDesc ray;
    ray.Origin = gOrigin + gRight * ((uv.x - 0.5f) * gSize) - gUp * ((uv.y - 0.5f) * gSize);
    ray.Direction = gDirection;
    ray.TMin = 0.0f;
    ray.TMax = gRange;

    // 葉などアルファで抜ける所は通す
    const RTSceneTables tables = MakeRTSceneTables(gHitInstanceTableIndex, gHitSubMeshTableIndex);
    const RTTraceHit hit = TraceRTClosestHit(gScene, tables, ray, kRTInstanceMaskShadowCaster, gTexelSize, 0.0f);
    gSunDepth[texel] = hit.hit ? hit.hitT : kRTVolumetricShadowMiss;
}
