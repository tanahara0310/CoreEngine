/// @file VolumetricFogLightScattering.CS.hlsl
/// @brief 格子 1 つずつに、視線の向きへ散乱する光を書く
/// @details 光の源は平行光源（位相関数つき）と空の平均の輝度。
///          出力: rgb = 散乱係数 × 光の源、a = 消散係数 [1/m]。

#include "VolumetricFogCommon.hlsli"
#include "DepthReconstruction.hlsli"

ConstantBuffer<VolumetricFogParameters> gVolumetricFog : register(b0);
Texture3D<float4> gVBuffer : register(t0);
StructuredBuffer<float4> gSkyIrradianceSH : register(t1);
RWTexture3D<float4> gLightScattering : register(u0);

[numthreads(4, 4, 4)]
void main(uint3 cell : SV_DispatchThreadID)
{
    const VolumetricFogParameters p = gVolumetricFog;
    if (any(cell >= p.gridSize))
    {
        return;
    }

    const float4 medium = gVBuffer[cell];
    if (medium.a <= 0.0f)
    {
        gLightScattering[cell] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    const float2 uv = (float2(cell.xy) + 0.5f) * p.gridPixelSize;
    const float3 farPoint = ReconstructWorldPosition(ScreenUVToNDC(uv), 1.0f, p.invViewProj);
    const float3 viewDir = normalize(farPoint - p.cameraWorldPos);

    const float3 ambient = VolumetricFogSkyAmbient(p, gSkyIrradianceSH);
    const float3 source = VolumetricFogUnshadowedSource(p, viewDir, ambient);
    gLightScattering[cell] = float4(medium.rgb * source, medium.a);
}
