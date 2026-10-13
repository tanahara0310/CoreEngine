// ============================================================
// しぶきの板（霧と、流体描画を使わないときのしぶき）の頂点
// ------------------------------------------------------------
// 1 粒 6 頂点。粒は生きている粒の一覧（しぶきか霧）から引く。描かない粒は大きさ 0 の三角形にする
// ============================================================

#include "WaterSprayView.hlsli"

StructuredBuffer<WaterSprayParticle> gParticles : register(t0);
StructuredBuffer<uint> gSprayIndices : register(t1);

struct WaterSprayParticleVSOutput
{
    float4 position : SV_POSITION;
    float2 corner : TEXCOORD0;       ///< 板の中の位置 [-1,1]²
    float viewDepth : TEXCOORD1;     ///< 粒の中心の、カメラの前方向の距離 [m]
    float opacity : TEXCOORD2;       ///< 不透明度
    float radius : TEXCOORD3;        ///< 描く半径 [m]
    nointerpolation uint kind : TEXCOORD4;
    float3 worldCenter : TEXCOORD5;
};

WaterSprayParticleVSOutput main(uint vertexId : SV_VertexID)
{
    WaterSprayParticleVSOutput output;
    output.position = float4(0.0f, 0.0f, 0.0f, 1.0f);
    output.corner = float2(0.0f, 0.0f);
    output.viewDepth = 0.0f;
    output.opacity = 0.0f;
    output.radius = 0.0f;
    output.kind = kSprayKindMist;
    output.worldCenter = float3(0.0f, 0.0f, 0.0f);

    const WaterSprayParticle p = gParticles[gSprayIndices[vertexId / 6u]];
    const bool drawn = IsSprayAlive(p) && (p.kind == kSprayKindMist || gFluidRendering == 0);
    if (!drawn)
    {
        return output;
    }

    const float2 corner = kSprayQuadCorners[vertexId % 6u];
    const float radius = ComputeSprayDrawRadius(p);
    const float3 world = p.kind == kSprayKindMist
        ? p.position + (corner.x * gCameraRight + corner.y * gCameraUp) * radius
        : ComputeSprayStretchedCorner(p, corner, radius);

    output.position = mul(float4(world, 1.0f), gViewProjection);
    output.corner = corner;
    output.viewDepth = ComputeSprayViewDepth(p.position);
    output.opacity = ComputeSprayLifeFade(p) * (p.kind == kSprayKindMist ? 0.12f : 0.9f);
    output.radius = radius;
    output.kind = p.kind;
    output.worldCenter = p.position;
    return output;
}
