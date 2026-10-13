// ============================================================
// 流体描画のしぶきの頂点（カメラへ向けた板に球を描く）
// ------------------------------------------------------------
// 1 粒 6 頂点。粒は生きているしぶきの一覧から引く。しぶき以外と死んだ粒は大きさ 0 の三角形にする
// ============================================================

#include "WaterSprayView.hlsli"

StructuredBuffer<WaterSprayParticle> gParticles : register(t0);
StructuredBuffer<uint> gSprayIndices : register(t1);

struct WaterSpraySplatVSOutput
{
    float4 position : SV_POSITION;
    float2 corner : TEXCOORD0;      ///< 板の中の位置 [-1,1]²
    float viewDepth : TEXCOORD1;    ///< 球の中心の、カメラの前方向の距離 [m]
    float radius : TEXCOORD2;       ///< 球の半径 [m]
    float2 motion : TEXCOORD3;      ///< 球の中心の NDC の動き（今 − 前）
};

WaterSpraySplatVSOutput main(uint vertexId : SV_VertexID)
{
    WaterSpraySplatVSOutput output;
    output.position = float4(0.0f, 0.0f, 0.0f, 1.0f);
    output.corner = float2(0.0f, 0.0f);
    output.viewDepth = 0.0f;
    output.radius = 0.0f;
    output.motion = float2(0.0f, 0.0f);

    const WaterSprayParticle p = gParticles[gSprayIndices[vertexId / 6u]];
    if (!IsSprayAlive(p) || p.kind != kSprayKindDroplet)
    {
        return output;
    }

    const float2 corner = kSprayQuadCorners[vertexId % 6u];
    const float radius = p.radius * kSpraySplatScale * saturate(ComputeSprayLifeFade(p) * 4.0f);
    const float3 world = ComputeSprayStretchedCorner(p, corner, radius);

    output.position = mul(float4(world, 1.0f), gViewProjection);
    output.corner = corner;
    output.viewDepth = ComputeSprayViewDepth(p.position);
    output.radius = radius;
    output.motion = ProjectSprayNdc(p.position, gViewProjection) - ProjectSprayNdc(p.prevPosition, gPrevViewProjection);
    return output;
}
