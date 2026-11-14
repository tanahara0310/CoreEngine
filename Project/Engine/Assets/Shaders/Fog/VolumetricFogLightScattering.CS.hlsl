/// @file VolumetricFogLightScattering.CS.hlsl
/// @brief 格子 1 つずつに、視線の向きへ散乱する光を書く
/// @details 光の源は平行光源（位相関数・雲の影・太陽から見た深さの画像による物の影つき）・
///          点光源・スポット・エリア（位相関数つき・影なし）と空の平均の輝度。
///          点は格子の中で毎フレームずらし、前フレームの格子へ写した位置の光と混ぜる。
///          出力: rgb = 散乱係数 × 光の源、a = 消散係数 [1/m]。
///          奥行き 0 の格子は、縦横 1 列の中心の視線で格子の端の点の光の源（物の影なし）も書く（格子より奥の霧の光）。

#include "VolumetricFogCommon.hlsli"
#include "DepthReconstruction.hlsli"

ConstantBuffer<VolumetricFogParameters> gVolumetricFog : register(b0);
ConstantBuffer<VolumetricFogLocalLights> gVolumetricFogLocalLights : register(b1);
Texture3D<float4> gVBuffer : register(t0);
StructuredBuffer<float4> gSkyIrradianceSH : register(t1);
Texture2D<float> gCloudShadowMap : register(t2);
Texture3D<float4> gLightScatteringHistory : register(t3);
Texture2D<float> gSunDepth : register(t4);
SamplerState gLinearClamp : register(s0);
RWTexture3D<float4> gLightScattering : register(u0);
RWTexture2D<float4> gFarLight : register(u1);

/// @brief 画面の UV と距離のスライス座標から、格子の中の点のワールド座標と視線を求める
float3 CellWorldPosition(VolumetricFogParameters p, float2 uv, float slice, out float3 viewDir)
{
    const float3 farPoint = ReconstructWorldPosition(ScreenUVToNDC(uv), 1.0f, p.invViewProj);
    viewDir = normalize(farPoint - p.cameraWorldPos);
    return p.cameraWorldPos + viewDir * VolumetricFogSliceToDistance(p, slice);
}

/// @brief 格子 1 つの対角の長さの半分 [m]
float CellRadius(VolumetricFogParameters p, uint3 cell)
{
    float3 dir;
    const float3 nearCorner = CellWorldPosition(p, float2(cell.xy) * p.gridPixelSize, float(cell.z), dir);
    const float3 farCorner = CellWorldPosition(p, float2(cell.xy + 1u) * p.gridPixelSize, float(cell.z + 1u), dir);
    return 0.5f * length(farCorner - nearCorner);
}

/// @brief 点光源・スポット・エリアから視線の向きへ散乱する光の源（散乱係数を掛ける前）
/// @param viewDir カメラから点へ向かう向き（正規化済み）
/// @param minDistance 逆二乗に使う距離の下限 [m]
float3 LocalLightSource(VolumetricFogParameters p, float3 worldPos, float3 viewDir, float minDistance)
{
    float3 source = float3(0.0f, 0.0f, 0.0f);
    [loop]
    for (uint i = 0; i < gVolumetricFogLocalLights.count; ++i)
    {
        float3 toLight;
        const float3 irradiance = VolumetricFogLocalLightIrradiance(
            gVolumetricFogLocalLights.lights[i], worldPos, minDistance, toLight);
        source += irradiance * VolumetricFogPhaseHG(p.phaseG, dot(viewDir, toLight));
    }
    return source;
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

/// @brief 縦横 1 列の中心の視線で、格子の端の点の光の源にアルベドを掛けた値（物の影は掛けない）
float3 FarLight(VolumetricFogParameters p, uint2 column, float3 ambient)
{
    float3 viewDir;
    const float2 uv = (float2(column) + 0.5f) * p.gridPixelSize;
    const float3 boundary = CellWorldPosition(p, uv, float(p.gridSize.z), viewDir);
    return p.albedo * VolumetricFogSource(p, viewDir, ambient, boundary, gCloudShadowMap, gLinearClamp, 1.0f);
}

[numthreads(4, 4, 4)]
void main(uint3 cell : SV_DispatchThreadID)
{
    const VolumetricFogParameters p = gVolumetricFog;
    if (any(cell >= p.gridSize))
    {
        return;
    }

    const float3 ambient = VolumetricFogSkyAmbient(p, gSkyIrradianceSH);
    if (cell.z == 0)
    {
        gFarLight[cell.xy] = float4(FarLight(p, cell.xy, ambient), 1.0f);
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

    const float sunDepthShadow = VolumetricFogSunDepthShadow(p, gSunDepth, gLinearClamp, worldPos);
    float3 source = VolumetricFogSource(
        p, viewDir, ambient, worldPos, gCloudShadowMap, gLinearClamp, sunDepthShadow);
    if (gVolumetricFogLocalLights.count > 0)
    {
        source += LocalLightSource(p, worldPos, viewDir, CellRadius(p, cell));
    }
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
