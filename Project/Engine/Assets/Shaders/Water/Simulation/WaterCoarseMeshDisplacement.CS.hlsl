// ============================================================
// 粗い格子の頂点の変位
// 水面メッシュの粗い格子（WaterSurfaceMeshGenerator の PlaneMeshGenerator の部分）の頂点ごとに、
// FFTWater.VS と同じ式の変位（xyz）と、その高さに入っている物が起こした波紋の高さ（w）を書き出す。
// RT の水面パスと水面の高さの問い合わせは、岸の格子の外の水面の高さをこの値の三角形補間で求める。
// テクセル (i, j) は変位前のワールド XZ = 粗い格子の最小の角 + (i, j) × 1 マスの幅（GetCoarseMeshVertexBaseXZ）。
// ============================================================

#include "../RayTracing/RTWaterSurfaceCommon.hlsli"

Texture2DArray<float4> gCoarseMeshDisplacementSource : register(t0);
RWTexture2D<float4> gCoarseMeshDisplacementOutput : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint width;
    uint height;
    gCoarseMeshDisplacementOutput.GetDimensions(width, height);
    if (any(dispatchThreadId.xy >= uint2(width, height)))
    {
        return;
    }

    const float2 baseXZ = GetCoarseMeshVertexBaseXZ(dispatchThreadId.xy);
    gCoarseMeshDisplacementOutput[dispatchThreadId.xy] = SampleMeshVertexDisplacementAndObjectRipple(
        gCoarseMeshDisplacementSource, baseXZ, GetCoarseMeshVertexSpacing());
}
