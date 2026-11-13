/// @file VolumetricFogIntegrate.CS.hlsl
/// @brief 格子の縦横 1 列ずつ、手前のスライスから奥へ光と透過率を積み上げる
/// @details 出力のスライス s には、カメラからスライス s の奥の面までに溜まった光（rgb）と透過率（a）を書く。

#include "VolumetricFogCommon.hlsli"

ConstantBuffer<VolumetricFogParameters> gVolumetricFog : register(b0);
Texture3D<float4> gLightScattering : register(t0);
RWTexture3D<float4> gIntegrated : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const VolumetricFogParameters p = gVolumetricFog;
    if (any(dispatchThreadId.xy >= p.gridSize.xy))
    {
        return;
    }

    float3 light = float3(0.0f, 0.0f, 0.0f);
    float transmittance = 1.0f;
    float sliceNear = 0.0f;

    [loop]
    for (uint s = 0; s < p.gridSize.z; ++s)
    {
        const uint3 cell = uint3(dispatchThreadId.xy, s);
        const float sliceFar = VolumetricFogSliceToDistance(p, float(s) + 1.0f);
        const float sliceLength = sliceFar - sliceNear;

        const float4 scattering = gLightScattering[cell];
        light += transmittance * VolumetricFogIntegrateSlice(scattering.rgb, scattering.a, sliceLength);
        transmittance *= exp(-scattering.a * sliceLength);

        gIntegrated[cell] = float4(light, transmittance);
        sliceNear = sliceFar;
    }
}
