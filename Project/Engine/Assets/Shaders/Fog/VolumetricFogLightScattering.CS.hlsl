/// @file VolumetricFogLightScattering.CS.hlsl
/// @brief 格子 1 つずつに、視線の向きへ散乱する光を書く
/// @details 光の源は平行光源（位相関数と雲の影つき）と空の平均の輝度。
///          点は格子の中で毎フレームずらし、前フレームの格子へ写した位置の光と混ぜる。
///          出力: rgb = 散乱係数 × 光の源、a = 消散係数 [1/m]。

#include "VolumetricFogCommon.hlsli"
#include "DepthReconstruction.hlsli"

ConstantBuffer<VolumetricFogParameters> gVolumetricFog : register(b0);
Texture3D<float4> gVBuffer : register(t0);
StructuredBuffer<float4> gSkyIrradianceSH : register(t1);
Texture2D<float> gCloudShadowMap : register(t2);
Texture3D<float4> gLightScatteringHistory : register(t3);
SamplerState gLinearClamp : register(s0);
RWTexture3D<float4> gLightScattering : register(u0);

/// @brief 画面の UV と距離のスライス座標から、格子の中の点のワールド座標と視線を求める
float3 CellWorldPosition(VolumetricFogParameters p, float2 uv, float slice, out float3 viewDir)
{
    const float3 farPoint = ReconstructWorldPosition(ScreenUVToNDC(uv), 1.0f, p.invViewProj);
    viewDir = normalize(farPoint - p.cameraWorldPos);
    return p.cameraWorldPos + viewDir * VolumetricFogSliceToDistance(p, slice);
}

/// @brief 格子の中心を前フレームの格子へ写し、そこに溜まっていた光を引く
/// @return 前フレームの格子の外なら false
bool SampleHistory(VolumetricFogParameters p, uint3 cell, out float3 history)
{
    history = float3(0.0f, 0.0f, 0.0f);

    float3 centerDir;
    const float2 centerUV = (float2(cell.xy) + 0.5f) * p.gridPixelSize;
    const float3 center = CellWorldPosition(p, centerUV, float(cell.z) + 0.5f, centerDir);

    const float4 prevClip = mul(float4(center, 1.0f), p.prevViewProj);
    if (prevClip.w <= 1.0e-4f)
    {
        return false;
    }
    const float2 prevNdc = prevClip.xy / prevClip.w;
    const float2 prevUV = float2(prevNdc.x * 0.5f + 0.5f, 0.5f - prevNdc.y * 0.5f);
    const float prevDistance = length(center - p.prevCameraWorldPos);
    if (any(prevUV < 0.0f) || any(prevUV > 1.0f) || prevDistance > p.distance)
    {
        return false;
    }

    const float3 uvw = VolumetricFogGridUVW(p, prevUV, prevDistance);
    history = gLightScatteringHistory.SampleLevel(gLinearClamp, uvw, 0).rgb;
    return true;
}

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

    float3 viewDir;
    const float2 uv = (float2(cell.xy) + 0.5f + p.frameJitter.xy) * p.gridPixelSize;
    const float3 worldPos = CellWorldPosition(p, uv, float(cell.z) + p.frameJitter.z, viewDir);

    const float3 ambient = VolumetricFogSkyAmbient(p, gSkyIrradianceSH);
    const float3 source = VolumetricFogSource(p, viewDir, ambient, worldPos, gCloudShadowMap, gLinearClamp);
    float3 scattered = medium.rgb * source;

    if (p.historyValid != 0)
    {
        float3 history;
        if (SampleHistory(p, cell, history))
        {
            scattered = lerp(scattered, history, p.historyWeight);
        }
    }

    gLightScattering[cell] = float4(scattered, medium.a);
}
