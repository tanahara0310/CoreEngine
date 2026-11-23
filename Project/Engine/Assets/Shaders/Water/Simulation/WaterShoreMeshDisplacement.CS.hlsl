// ============================================================
// 岸の細かい格子の頂点の変位
// 岸の格子（WaterSurfaceMeshGenerator）の頂点ごとに、変位（xyz）と、その高さに入っている物が起こした波紋の高さ（w）、
// 沖から寄せる波の傾きと砕けた波の泡を書き出す。
// FFTWater.VS は岸の格子の頂点をこの値で動かす。RT の水面パスと水面の高さの問い合わせは、
// 描かれた水面の高さをこの値の三角形補間で求める。
// テクセル (i, j) は変位前のワールド XZ = 範囲の最小の角 + (i, j) × 1 マスの幅 を
// 水面メッシュの範囲へ寄せた頂点（GetShoreMeshVertexBaseXZ）。
// ============================================================

#include "../RayTracing/RTWaterSurfaceCommon.hlsli"

Texture2DArray<float4> gShoreMeshDisplacementSource : register(t0);
RWTexture2D<float4> gShoreMeshDisplacementOutput : register(u0);
// xy = 沖から寄せる波の傾き（∂高さ/∂x, ∂高さ/∂z）、zw = 砕けた波の泡（被覆率・段と浜を上がる水の泡を描く度合い）
RWTexture2D<float4> gShoreMeshSurfaceOutput : register(u1);

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

    const float2 baseXZ = GetShoreMeshVertexBaseXZ(dispatchThreadId.xy);
    float2 shoreWaveSlope;
    float2 shoreBreakFoam;
    gShoreMeshDisplacementOutput[dispatchThreadId.xy] = SampleShoreMeshVertexDisplacementAndObjectRipple(
        gShoreMeshDisplacementSource, baseXZ, shoreWaveSlope, shoreBreakFoam);
    gShoreMeshSurfaceOutput[dispatchThreadId.xy] = float4(shoreWaveSlope, shoreBreakFoam);
}
