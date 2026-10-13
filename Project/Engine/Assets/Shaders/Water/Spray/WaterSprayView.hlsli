// ============================================================
// しぶきの描画の共通部分（カメラの定数・光・空・シーンの深度）
// ------------------------------------------------------------
// 定数の並びは C++ の WaterSpraySystem::ViewConstants と同じ
// ============================================================
#ifndef WATER_SPRAY_VIEW_INCLUDED
#define WATER_SPRAY_VIEW_INCLUDED

#include "WaterSpray.hlsli"
#include "../../Include/Lighting/LightStructures.hlsli"
#include "../../Include/Common/DepthReconstruction.hlsli"

cbuffer WaterSprayViewConstants : register(b0)
{
    float4x4 gViewProjection;      // 今のフレームの View*Projection（ジッタ込み）
    float4x4 gPrevViewProjection;  // 前のフレームの View*Projection（ジッタ込み）
    float3 gCameraPosition;        // カメラのワールド位置
    float gProjScaleX;             // 投影行列の _11
    float3 gCameraRight;           // カメラの右（ワールド）
    float gProjScaleY;             // 投影行列の _22
    float3 gCameraUp;              // カメラの上（ワールド）
    uint gFluidRendering;          // 1 = しぶきを流体描画で描く
    float3 gCameraForward;         // カメラの前（ワールド）
    float gSkyAmbientScale;        // 空の放射照度に掛ける倍率
    float2 gScreenSize;            // 画面の大きさ [px]
    float2 gProjZW;                // 投影行列の (_33, _43)
    float3 gWaterAbsorption;       // 水の吸収係数 [1/m]
    uint gSkyAmbientEnabled;       // 1 = 空の放射照度（SH）を使う
    uint gSkyEnvironmentEnabled;   // 1 = 空のキューブマップを映す
    float3 gViewPad;
};

ConstantBuffer<LightCounts> gLightCounts : register(b1);
StructuredBuffer<DirectionalLightData> gDirectionalLights : register(t8);
StructuredBuffer<float4> gWaterSkyIrradianceSH : register(t9);
TextureCube<float4> gSkyEnvironmentMap : register(t10);
Texture2D<float> gSceneDepth : register(t11);
SamplerState gLinearClamp : register(s0);

/// @brief 空のキューブマップのミップの数
static const float kSprayEnvMipCount = 5.0f;
/// @brief 空気を含んだしぶきの白さ（拡散反射率）
static const float3 kSprayFoamAlbedo = float3(0.85f, 0.88f, 0.9f);

/// @brief ワールド位置の、カメラの前方向の距離 [m]
float ComputeSprayViewDepth(float3 worldPosition)
{
    return dot(worldPosition - gCameraPosition, gCameraForward);
}

/// @brief カメラの前方向の距離から深度バッファの値（標準 Z）を求める
float ViewDepthToSprayNdcDepth(float viewDepth)
{
    return gProjZW.x + gProjZW.y / max(viewDepth, 1.0e-4f);
}

/// @brief 画素のシーンの深度（標準 Z）
float LoadSpraySceneDepth(int2 pixel)
{
    return gSceneDepth.Load(int3(pixel, 0));
}

/// @brief 画素のシーンの、カメラの前方向の距離 [m]（何も無い画素は十分に遠い値）
float LoadSpraySceneViewDepth(int2 pixel)
{
    const float depth = LoadSpraySceneDepth(pixel);
    if (IsBackgroundDepth(depth))
    {
        return 1.0e6f;
    }
    return LinearizeViewDepth(depth, gProjZW);
}

/// @brief 法線の向きの空の放射照度 / π（SH9。空が無いときは 0）
float3 EvaluateSpraySkyIrradiance(float3 n)
{
    if (gSkyAmbientEnabled == 0)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }
    float basis[9];
    basis[0] = 0.282095f;
    basis[1] = 0.488603f * n.y;
    basis[2] = 0.488603f * n.z;
    basis[3] = 0.488603f * n.x;
    basis[4] = 1.092548f * n.x * n.y;
    basis[5] = 1.092548f * n.y * n.z;
    basis[6] = 0.315392f * (3.0f * n.z * n.z - 1.0f);
    basis[7] = 1.092548f * n.x * n.z;
    basis[8] = 0.546274f * (n.x * n.x - n.y * n.y);

    float3 irradiance = float3(0.0f, 0.0f, 0.0f);
    [unroll]
    for (int i = 0; i < 9; ++i)
    {
        irradiance += gWaterSkyIrradianceSH[i].rgb * basis[i];
    }
    return max(irradiance, 0.0f) * gSkyAmbientScale;
}

/// @brief 平行光源（太陽・月）の拡散光 E·(n·l の回り込み) / π
/// @param wrap 光の当たらない側へ回り込ませる量（0 = Lambert）
float3 EvaluateSprayDirectDiffuse(float3 n, float wrap)
{
    float3 lighting = float3(0.0f, 0.0f, 0.0f);
    for (uint i = 0; i < gLightCounts.directionalLightCount; ++i)
    {
        if (gDirectionalLights[i].enabled == 0)
        {
            continue;
        }
        const float3 lightVec = -normalize(gDirectionalLights[i].direction);
        const float ndotl = saturate((dot(n, lightVec) + wrap) / (1.0f + wrap));
        lighting += gDirectionalLights[i].color.rgb * gDirectionalLights[i].intensity * ndotl / kSprayPi;
    }
    return lighting;
}

/// @brief 平行光源を霧が散らす光（Henyey-Greenstein の前方散乱）
/// @param toCamera 粒からカメラへの向き
float3 EvaluateSprayDirectScattering(float3 toCamera, float anisotropy)
{
    float3 lighting = float3(0.0f, 0.0f, 0.0f);
    const float g2 = anisotropy * anisotropy;
    for (uint i = 0; i < gLightCounts.directionalLightCount; ++i)
    {
        if (gDirectionalLights[i].enabled == 0)
        {
            continue;
        }
        const float3 lightTravel = normalize(gDirectionalLights[i].direction);
        const float cosTheta = dot(lightTravel, toCamera);
        const float phase = (1.0f - g2) / (4.0f * kSprayPi * pow(max(1.0f + g2 + 2.0f * anisotropy * cosTheta, 1.0e-4f), 1.5f));
        lighting += gDirectionalLights[i].color.rgb * gDirectionalLights[i].intensity * phase;
    }
    return lighting;
}

/// @brief 平行光源の鏡面反射（GGX。幾何減衰は 1 とする）
float3 EvaluateSprayDirectSpecular(float3 n, float3 toCamera, float roughness, float f0)
{
    const float alpha = roughness * roughness;
    const float alpha2 = alpha * alpha;
    const float ndotv = max(dot(n, toCamera), 1.0e-3f);
    float3 lighting = float3(0.0f, 0.0f, 0.0f);
    for (uint i = 0; i < gLightCounts.directionalLightCount; ++i)
    {
        if (gDirectionalLights[i].enabled == 0)
        {
            continue;
        }
        const float3 lightVec = -normalize(gDirectionalLights[i].direction);
        const float ndotl = saturate(dot(n, lightVec));
        if (ndotl <= 0.0f)
        {
            continue;
        }
        const float3 halfVec = normalize(lightVec + toCamera);
        const float ndoth = saturate(dot(n, halfVec));
        const float denom = ndoth * ndoth * (alpha2 - 1.0f) + 1.0f;
        const float distribution = alpha2 / (kSprayPi * denom * denom);
        const float fresnel = f0 + (1.0f - f0) * pow(1.0f - saturate(dot(halfVec, toCamera)), 5.0f);
        lighting += gDirectionalLights[i].color.rgb * gDirectionalLights[i].intensity
            * distribution * fresnel * ndotl / (4.0f * ndotv * max(ndotl, 1.0e-3f));
    }
    return lighting;
}

/// @brief 向き direction の空（キューブマップ。下向きは水平線で折り返す）
float3 SampleSpraySky(float3 direction, float roughness)
{
    float3 dir = direction;
    dir.y = max(abs(dir.y), 0.01f);
    if (gSkyEnvironmentEnabled == 0)
    {
        return EvaluateSpraySkyIrradiance(normalize(dir)) * kSprayPi;
    }
    return gSkyEnvironmentMap.SampleLevel(gLinearClamp, dir, roughness * (kSprayEnvMipCount - 1.0f)).rgb;
}

/// @brief 空気を含んだしぶきが光を中で散らして、光の当たらない側へ回り込ませる量
static const float kSprayFoamWrap = 1.0f;

/// @brief 空気を含んだしぶき（泡と同じ白い面）の色
float3 ComputeSprayFoamColor(float3 n)
{
    return kSprayFoamAlbedo * (EvaluateSprayDirectDiffuse(n, kSprayFoamWrap) + EvaluateSpraySkyIrradiance(n));
}

/// @brief しぶきを速さの向きに伸ばす時間 [s]（伸びる長さ = 画面に沿った速さ × この時間）
static const float kSprayStretchTime = 0.015f;
/// @brief しぶきを伸ばす倍率の上限（半径に対する長さ）
static const float kSprayMaxStretch = 2.0f;
/// @brief 流体描画でしぶきを描く球の半径の、粒の半径に対する倍率
static const float kSpraySplatScale = 2.0f;
/// @brief 流体描画の厚みに掛ける倍率（球の半径の倍率の 2 乗の逆数）
static const float kSpraySplatThicknessScale = 1.0f / (kSpraySplatScale * kSpraySplatScale);

/// @brief しぶきの板の頂点のワールド位置（画面に沿った速さの向きに伸ばす）
float3 ComputeSprayStretchedCorner(WaterSprayParticle p, float2 corner, float radius)
{
    const float2 screenVelocity = float2(dot(p.velocity, gCameraRight), dot(p.velocity, gCameraUp));
    const float speed = length(screenVelocity);
    const float2 axis = speed > 1.0e-3f ? screenVelocity / speed : float2(1.0f, 0.0f);
    const float3 axisU = axis.x * gCameraRight + axis.y * gCameraUp;
    const float3 axisV = -axis.y * gCameraRight + axis.x * gCameraUp;
    const float stretch = 1.0f + min(speed * kSprayStretchTime / max(radius, 1.0e-3f), kSprayMaxStretch);
    return p.position + (corner.x * stretch * axisU + corner.y * axisV) * radius;
}

/// @brief ワールド位置の NDC の xy（行ベクトル規約）
float2 ProjectSprayNdc(float3 worldPosition, float4x4 viewProjection)
{
    const float4 clip = mul(float4(worldPosition, 1.0f), viewProjection);
    return clip.xy / max(clip.w, 1.0e-6f);
}

#endif // WATER_SPRAY_VIEW_INCLUDED
