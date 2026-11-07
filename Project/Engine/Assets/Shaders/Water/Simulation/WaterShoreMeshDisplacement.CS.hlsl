// ============================================================
// 岸の細かい格子の頂点の変位
// 岸の格子（WaterSurfaceMeshGenerator）の頂点ごとに、FFTWater.VS と同じ式の変位を書き出す。
// RT の水面パスと水面の高さの問い合わせは、描かれた水面の高さをこの値の三角形補間で求める。
// テクセル (i, j) は変位前のワールド XZ = 範囲の最小の角 + (i, j) × 1 マスの幅 の頂点。
// ============================================================

#include "../RayTracing/RTWaterSurfaceCommon.hlsli"

Texture2DArray<float4> gShoreMeshDisplacementSource : register(t0);
RWTexture2D<float4> gShoreMeshDisplacementOutput : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint width;
    uint height;
    gShoreMeshDisplacementOutput.GetDimensions(width, height);
    if (any(dispatchThreadId.xy >= uint2(width, height)))
    {
        return;
    }

    const float2 baseXZ = gSurfaceShoreMeshOriginXZ + float2(dispatchThreadId.xy) * gSurfaceShoreMeshCellSize;
    const float3 displacement = SampleShoreMeshVertexDisplacement(gShoreMeshDisplacementSource, baseXZ);
    gShoreMeshDisplacementOutput[dispatchThreadId.xy] = float4(displacement, 0.0f);
}
