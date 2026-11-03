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

/// @brief ワールド XZ の真上・真下にある水面の、基準面からの高さ
float EvaluateQuerySurfaceOffset(float2 worldXZ)
{
    if (UseFFTOceanSurface())
    {
        return EvaluateDrawnSurfaceHeight(gQueryDisplacement, worldXZ);
    }

    // Gerstner の水面：描かれた位置から水平の変位をたどり直して変位前の位置を求め、その高さを取る
    float2 baseXZ = worldXZ;
    [unroll]
    for (int iteration = 0; iteration < kGerstnerInverseIterations; ++iteration)
    {
        baseXZ = worldXZ - EvaluateWaterOffsetGerstner(baseXZ).xz;
    }
    return EvaluateWaterOffsetGerstner(baseXZ).y;
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

/// @brief 点 1 つの水面の高さ（ワールド Y）と傾き（dY/dX, dY/dZ）を書き出す
/// @details 出力は点ごとに 16 バイト（高さ, X の傾き, Z の傾き, 印）
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
    uint flags = 0u;
    if (IsInsideWaterRegion(worldXZ))
    {
        const float step = max(gQuerySlopeStep, 1.0e-3f);
        height += EvaluateQuerySurfaceOffset(worldXZ);
        const float heightPosX = EvaluateQuerySurfaceOffset(worldXZ + float2(step, 0.0f));
        const float heightNegX = EvaluateQuerySurfaceOffset(worldXZ - float2(step, 0.0f));
        const float heightPosZ = EvaluateQuerySurfaceOffset(worldXZ + float2(0.0f, step));
        const float heightNegZ = EvaluateQuerySurfaceOffset(worldXZ - float2(0.0f, step));
        slope = float2(heightPosX - heightNegX, heightPosZ - heightNegZ) / (2.0f * step);
        flags = kWaterHeightQueryInWater;
    }

    gQueryResults.Store4(pointIndex * 16u, uint4(asuint(height), asuint(slope.x), asuint(slope.y), flags));
}
