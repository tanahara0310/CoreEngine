// ============================================================
// 岸の細かい格子（WaterSurfaceMeshGenerator）
// ------------------------------------------------------------
// 水面のメッシュは、遠くまで覆う粗い格子と、海底の高さを測る範囲に置く岸の細かい格子の 2 枚。
// 範囲の中は岸の格子が描き、粗い格子は範囲の中を描かない。
// 岸の格子は範囲の端ほど粗い格子の三角形で補間した形へ寄せ、2 枚の境目で形をつなぐ。
// 利用側: FFTWater.VS / Water.VS / Water.PS / RTWaterSurfaceCommon
// ============================================================
#ifndef WATER_SHORE_MESH_INCLUDED
#define WATER_SHORE_MESH_INCLUDED

/// @brief 頂点が岸の格子のものか（WaterSurfaceMeshGenerator が animData.z に 1 を入れる）
bool IsShoreMeshVertex(float4 animData)
{
    return animData.z > 0.5f;
}

/// @brief 変位前の位置が岸の格子の範囲の中か
/// @param originXZ 範囲の XZ の最小の角 [m]
/// @param size     範囲の一辺の長さ [m]
bool IsInsideShoreMesh(float2 baseXZ, float2 originXZ, float size)
{
    const float2 local = baseXZ - originXZ;
    return all(local >= 0.0f) && all(local <= size);
}

/// @brief 岸の格子の形を、粗い格子の形から岸の格子の形へ移す重み（範囲の端で 0、端から blendWidth より内側で 1）
float ComputeShoreMeshBlend(float2 baseXZ, float2 originXZ, float size, float blendWidth)
{
    const float2 local = baseXZ - originXZ;
    const float2 edge = min(local, size - local);
    return saturate(min(edge.x, edge.y) / max(blendWidth, 1.0e-4f));
}

/// @brief 格子の三角形のうち、点を含むものの 3 頂点と重み
struct WaterGridTriangle
{
    float2 corner0;
    float2 corner1;
    float2 corner2;
    float3 weights;
};

/// @brief 点を含む格子の三角形を返す（各マスの対角は (1,0)-(0,1)。PlaneMeshGenerator と同じ）
/// @param gridOrigin 格子の XZ の最小の角 [m]
/// @param cellSize   1 マスの幅 [m]
WaterGridTriangle FindWaterGridTriangle(float2 xz, float2 gridOrigin, float2 cellSize)
{
    const float2 gridCoord = (xz - gridOrigin) / cellSize;
    const float2 cellIndex = floor(gridCoord);
    const float2 f = gridCoord - cellIndex;
    const float2 cellCorner = gridOrigin + cellIndex * cellSize;

    WaterGridTriangle tri;
    tri.corner1 = cellCorner + float2(cellSize.x, 0.0f);
    tri.corner2 = cellCorner + float2(0.0f, cellSize.y);
    if (f.x + f.y <= 1.0f)
    {
        tri.corner0 = cellCorner;
        tri.weights = float3(1.0f - f.x - f.y, f.x, f.y);
    }
    else
    {
        tri.corner0 = cellCorner + cellSize;
        tri.weights = float3(f.x + f.y - 1.0f, 1.0f - f.y, 1.0f - f.x);
    }
    return tri;
}

#endif // WATER_SHORE_MESH_INCLUDED
