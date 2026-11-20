// ============================================================
// 水面の高さの問い合わせ
// 点ごとに、描かれる水面の高さと傾きを求めて書き出す。
// 水面の形は RT の水面パスと同じ関数（RTWaterSurfaceCommon.hlsli）で評価する。
// ============================================================

#include "../RayTracing/RTWaterSurfaceCommon.hlsli"

/// @brief 1 回に問い合わせられる点の数（WaterQuery::kMaxPoints と同じ値）
static const uint kWaterHeightQueryMaxPoints = 1024;

cbuffer WaterHeightQueryConstants : register(b0)
{
    uint gQueryPointCount;   // 評価する点の数
    float gQuerySlopeStep;   // 傾きを差分で求める間隔 [m]
    float2 gQueryPad;
    float4 gQueryPoints[kWaterHeightQueryMaxPoints / 2]; // 点 2 つずつのワールド XZ (x0, z0, x1, z1)
};

Texture2DArray<float4> gQueryDisplacement : register(t0);
RWByteAddressBuffer gQueryResults : register(u0);

/// @brief 結果の印：水面の範囲の中
static const uint kWaterHeightQueryInWater = 1u;

/// @brief Gerstner の水面で、描かれた位置から変位前の位置を求めるときに水平変位をたどり直す回数
static const int kGerstnerInverseIterations = 4;

/// @brief ワールド XZ の真上・真下にある水面の、基準面からの高さ（x）と、そのうち物が起こした波紋の高さ（y）
/// @details 物が起こした波紋の高さは、水面の高さと同じ頂点・同じ三角形で補間する
float2 EvaluateQuerySurfaceOffset(float2 worldXZ)
{
    if (UseFFTOceanSurface())
    {
        return EvaluateDrawnSurfaceHeightAndObjectRipple(gQueryDisplacement, worldXZ);
    }

    // Gerstner の水面：描かれた位置から水平の変位をたどり直して変位前の位置を求め、その高さを取る。
    // 波紋は描かれた位置で読む
    float2 baseXZ = worldXZ;
    [unroll]
    for (int iteration = 0; iteration < kGerstnerInverseIterations; ++iteration)
    {
        baseXZ = worldXZ - EvaluateWaterOffsetGerstner(baseXZ).xz;
    }
    const float2 ripple = EvaluateSurfaceRippleVertexHeights(worldXZ, GetCoarseMeshVertexSpacing());
    return float2(EvaluateWaterOffsetGerstner(baseXZ).y + ripple.x, ripple.y);
}

/// @brief ワールド XZ の真上・真下の水面の高さに入っている波紋の高さ [m]
/// @details その点を覆う水面メッシュの頂点の間隔で、形に入る段の波紋をその点で評価する
float EvaluateQueryRippleHeight(float2 worldXZ)
{
    float vertexSpacing = GetCoarseMeshVertexSpacing();
    if (UseFFTOceanSurface() && gSurfaceShoreMeshEnabled != 0
        && IsInsideShoreMesh(worldXZ, gSurfaceShoreMeshOriginXZ, gSurfaceShoreMeshSize))
    {
        vertexSpacing = gSurfaceShoreMeshCellSize;
    }
    return EvaluateSurfaceRippleVertexHeight(worldXZ, vertexSpacing);
}

/// @brief 点が水面メッシュの範囲の中か
bool IsInsideWaterRegion(float2 worldXZ)
{
    if (gSurfaceRegionValid == 0)
    {
        return false;
    }
    return all(abs(worldXZ - gSurfaceRegionCenterXZ) <= gSurfaceRegionHalfExtentXZ);
}

/// @brief 点 1 つの水面の高さ（ワールド Y）と傾き（dY/dX, dY/dZ）と、高さに入っている波紋の高さと傾きを書き出す
/// @details 出力は点ごとに 48 バイト（高さ, X の傾き, Z の傾き, 印,
///          波紋の高さ, 波紋の X の傾き, 波紋の Z の傾き, 物が起こした波紋の高さ,
///          物が起こした波紋の X の傾き, 物が起こした波紋の Z の傾き, 0, 0）
[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint pointIndex = dispatchThreadId.x;
    if (pointIndex >= gQueryPointCount)
    {
        return;
    }

    const float4 packed = gQueryPoints[pointIndex >> 1];
    const float2 worldXZ = (pointIndex & 1u) != 0 ? packed.zw : packed.xy;

    float height = gSurfaceWaterHeight;
    float2 slope = float2(0.0f, 0.0f);
    float rippleHeight = 0.0f;
    float2 rippleSlope = float2(0.0f, 0.0f);
    float objectRippleHeight = 0.0f;
    float2 objectRippleSlope = float2(0.0f, 0.0f);
    uint flags = 0u;
    if (IsInsideWaterRegion(worldXZ))
    {
        const float step = max(gQuerySlopeStep, 1.0e-3f);
        const float2 stepX = float2(step, 0.0f);
        const float2 stepZ = float2(0.0f, step);

        // x = 水面の高さ・y = そのうち物が起こした波紋の高さ
        const float2 center = EvaluateQuerySurfaceOffset(worldXZ);
        const float2 posX = EvaluateQuerySurfaceOffset(worldXZ + stepX);
        const float2 negX = EvaluateQuerySurfaceOffset(worldXZ - stepX);
        const float2 posZ = EvaluateQuerySurfaceOffset(worldXZ + stepZ);
        const float2 negZ = EvaluateQuerySurfaceOffset(worldXZ - stepZ);
        height += center.x;
        slope = float2(posX.x - negX.x, posZ.x - negZ.x) / (2.0f * step);
        objectRippleHeight = center.y;
        objectRippleSlope = float2(posX.y - negX.y, posZ.y - negZ.y) / (2.0f * step);

        rippleHeight = EvaluateQueryRippleHeight(worldXZ);
        rippleSlope = float2(
            EvaluateQueryRippleHeight(worldXZ + stepX) - EvaluateQueryRippleHeight(worldXZ - stepX),
            EvaluateQueryRippleHeight(worldXZ + stepZ) - EvaluateQueryRippleHeight(worldXZ - stepZ)) / (2.0f * step);
        flags = kWaterHeightQueryInWater;
    }

    const uint base = pointIndex * 48u;
    gQueryResults.Store4(base, uint4(asuint(height), asuint(slope.x), asuint(slope.y), flags));
    gQueryResults.Store4(base + 16u,
        uint4(asuint(rippleHeight), asuint(rippleSlope.x), asuint(rippleSlope.y), asuint(objectRippleHeight)));
    gQueryResults.Store4(base + 32u, uint4(asuint(objectRippleSlope.x), asuint(objectRippleSlope.y), 0u, 0u));
}
