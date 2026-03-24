#include "FullScreen.hlsli"
#include "../Common/LightStructures.hlsli"
#include "../Common/ShadowCalculation.hlsli"
#include "../Common/PBR.hlsli"

// ============================================================
// G-Buffer テクスチャ
// ============================================================
Texture2D<float4> gAlbedoAO : register(t0);
Texture2D<float4> gNormalRoughness : register(t1);
Texture2D<float4> gEmissiveMetallic : register(t2);
Texture2D<float4> gWorldPosition : register(t3);

// ============================================================
// ライトデータ
// ============================================================
ConstantBuffer<LightCounts> gLightCounts : register(b1);
StructuredBuffer<DirectionalLightData> gDirectionalLights : register(t4);
StructuredBuffer<PointLightData> gPointLights : register(t5);
StructuredBuffer<SpotLightData> gSpotLights : register(t6);
StructuredBuffer<AreaLightData> gAreaLights : register(t7);

// ============================================================
// シャドウ
// ============================================================
Texture2D<float> gShadowMap : register(t8);
SamplerComparisonState gShadowSampler : register(s1);

struct LightVP
{
    float4x4 mat;
};
ConstantBuffer<LightVP> gLightViewProjection : register(b3);

// ============================================================
// カメラ
// ============================================================
struct Camera
{
    float3 worldPosition;
};
ConstantBuffer<Camera> gCamera : register(b2);

// ============================================================
// IBL テクスチャ
// ============================================================
TextureCube<float4> gIrradianceMap : register(t9);
TextureCube<float4> gPrefilteredMap : register(t10);
Texture2D<float2> gBRDFLUT : register(t11);

// ============================================================
// IBL パラメータ（シーン共通）
// ============================================================
struct IBLParams
{
    float3 environmentRotation; // 環境マップ XYZ 回転（ラジアン）
    float iblIntensity;         // IBL 強度 (0.0-∞, デフォルト 1.0)
};
ConstantBuffer<IBLParams> gIBLParams : register(b4);

SamplerState gSampler : register(s0);

// ============================================================
// ACES トーンマッピング
// ============================================================
float3 ACESFilm(float3 x)
{
    const float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// ============================================================
// IBL（PBR.hlsli CalculateFullIBL と同じロジック）
// ============================================================
float3 CalculateDeferredIBL(float3 N, float3 V, float3 albedo, float metallic, float roughness, float3 F0, float ao)
{
    float NdotV = max(dot(N, V), 0.0f);
    float3 F = FresnelSchlickRoughness(NdotV, F0, roughness);

    // === Diffuse IBL ===
    float3 rotatedN = RotateVector(N, gIBLParams.environmentRotation);
    float3 irradiance = gIrradianceMap.SampleLevel(gSampler, rotatedN, 0.0f).rgb;
    float3 kD = (1.0f - F) * (1.0f - metallic);
    float3 diffuseIBL = kD * albedo * irradiance;

    // === Specular IBL ===
    float3 R = normalize(reflect(-V, N));
    float3 rotatedR = RotateVector(R, gIBLParams.environmentRotation);
    float mipLevel = roughness * float(MAX_PREFILTERED_MIP_LEVELS - 1);
    float3 prefilteredColor = gPrefilteredMap.SampleLevel(gSampler, rotatedR, mipLevel).rgb;
    float2 envBRDF = gBRDFLUT.Sample(gSampler, float2(NdotV, roughness));
    float3 specularIBL = prefilteredColor * (F * envBRDF.x + envBRDF.y);

    return (diffuseIBL + specularIBL) * ao * gIBLParams.iblIntensity;
}

// ============================================================
// 入出力
// ============================================================
struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};
struct PixelShaderOutput
{
    float4 color : SV_Target;
};

// ============================================================
// メインシェーダー
// ============================================================
PixelShaderOutput main(PixelShaderInput input)
{
    PixelShaderOutput output;

    // G-Buffer は Load() でポイントサンプリング（バイリニア補間によるフラグ/法線の破損を防止）
    int3 loadCoord = int3(input.position.xy, 0);
    float4 albedoAO = gAlbedoAO.Load(loadCoord);
    float4 normalRoughness = gNormalRoughness.Load(loadCoord);
    float4 emissiveMetallic = gEmissiveMetallic.Load(loadCoord);
    float4 worldPosSample = gWorldPosition.Load(loadCoord);

    // ===== 背景ピクセル検出 =====
    // GBuffer は worldPosition.a >= 1.0 を書き込む。
    // クリア後の背景ピクセルは a = 0 なのでここで早期リターン。
    if (worldPosSample.a < 0.5f)
    {
        output.color = float4(0.1f, 0.25f, 0.5f, 1.0f);
        return output;
    }

    // ===== マテリアルフラグのデコード =====
    // GBuffer.PS.hlsl が worldPosition.a にエンコードしたフラグを読み取る。
    // 2 = PBR + IBL無効, 3 = PBR + IBL有効
    int pixelType = int(round(worldPosSample.a));

    // ===== アンリットマテリアル検出 =====
    // GBuffer.PS.hlsl は enableLighting==0 のとき normalRoughness.a=0 のセンチネル値を書き込み、
    // emissiveMetallic.rgb にアンリットカラーを格納する。
    if (normalRoughness.a <= 0.0f)
    {
        output.color = float4(emissiveMetallic.rgb, 1.0f);
        return output;
    }

    // 共通パラメータ展開
    float3 albedo   = albedoAO.rgb;
    float3 N        = normalize(normalRoughness.rgb * 2.0f - 1.0f);
    float3 worldPos = worldPosSample.xyz;
    float3 V        = normalize(gCamera.worldPosition - worldPos);

    // ===== ライト空間座標（シャドウ計算用） =====
    float4 lightSpacePos = mul(float4(worldPos, 1.0f), gLightViewProjection.mat);

    // ============================================================
    // PBR ライティングパス（常にここに到達）
    // ============================================================
    const bool enableIBL = (pixelType >= 3);

    float ao       = saturate(albedoAO.a);
    float roughness = saturate(normalRoughness.a);
    float metallic = saturate(emissiveMetallic.a);
    float3 emissive = emissiveMetallic.rgb;
    float3 F0 = lerp(float3(DIELECTRIC_F0, DIELECTRIC_F0, DIELECTRIC_F0), albedo, metallic);

    float3 Lo = float3(0.0f, 0.0f, 0.0f);

    // ディレクショナルライト
    for (uint di = 0; di < gLightCounts.directionalLightCount; ++di)
    {
        DirectionalLightData dL = gDirectionalLights[di];
        if (!dL.enabled)
            continue;
        float3 L = normalize(-dL.direction);

        float shadowFactor = CalculateShadow(lightSpacePos, N, dL.direction, gShadowMap, gShadowSampler);
        shadowFactor = lerp(0.3f, 1.0f, shadowFactor);

        Lo += CalculatePBRLighting(N, V, L, dL.color.rgb, dL.intensity, albedo, metallic, roughness, ao)
              * shadowFactor;
    }

    // ポイントライト（PBR: 逆二乗則 + 距離レンジ減衰）
    for (uint pi = 0; pi < gLightCounts.pointLightCount; ++pi)
    {
        PointLightData pL = gPointLights[pi];
        if (!pL.enabled)
            continue;
        float3 tv = pL.position - worldPos;
        float d = length(tv);
        if (d >= pL.radius)
            continue;
        float3 L = normalize(tv);
        float attenuation = 1.0f / (1.0f + pL.decay * d * d);
        float rangeFactor = saturate(1.0f - d / pL.radius);
        attenuation *= rangeFactor;
        Lo += CalculatePBRLighting(N, V, L, pL.color.rgb, pL.intensity * attenuation, albedo, metallic, roughness, ao);
    }

    // スポットライト（PBR: 逆二乗則 + コーン減衰）
    for (uint si = 0; si < gLightCounts.spotLightCount; ++si)
    {
        SpotLightData sL = gSpotLights[si];
        if (!sL.enabled)
            continue;
        float3 tv = sL.position - worldPos;
        float d = length(tv);
        if (d >= sL.distance)
            continue;
        float3 L = normalize(tv);
        float attenuation = 1.0f / (1.0f + sL.decay * d * d);
        float rangeFactor = saturate(1.0f - d / sL.distance);
        attenuation *= rangeFactor;
        float cosTheta = dot(-L, normalize(sL.direction));
        if (cosTheta < sL.cosAngle)
            continue;
        float spotFactor = saturate((cosTheta - sL.cosAngle) / (sL.cosFalloffStart - sL.cosAngle));
        attenuation *= spotFactor;
        Lo += CalculatePBRLighting(N, V, L, sL.color.rgb, sL.intensity * attenuation, albedo, metallic, roughness, ao);
    }

    // エリアライト（PBR: 最近接点計算 + PBR ライティング）
    for (uint ai = 0; ai < gLightCounts.areaLightCount; ++ai)
    {
        AreaLightData aL = gAreaLights[ai];
        if (!aL.enabled)
            continue;

        // 最近接点計算（Forward の Lighting.hlsli CalculateAreaLight と同等）
        float3 toLight = aL.position - worldPos;
        float distToPlane = dot(toLight, aL.normal);
        float3 projectedPoint = worldPos + aL.normal * distToPlane;
        float3 offset = projectedPoint - aL.position;
        float u = dot(offset, aL.right);
        float v = dot(offset, aL.up);
        float halfW = aL.width * 0.5f;
        float halfH = aL.height * 0.5f;
        float clampedU = clamp(u, -halfW, halfW);
        float clampedV = clamp(v, -halfH, halfH);
        float3 closestPoint = aL.position + aL.right * clampedU + aL.up * clampedV;

        float3 toClosest = closestPoint - worldPos;
        float dist = length(toClosest);
        if (dist >= aL.range)
            continue;
        float3 L = toClosest / max(dist, 0.001f);

        float distFactor = 1.0f - saturate(dist / aL.range);
        float distAttenuation = distFactor * distFactor;

        float shapeFactor = 1.0f;
        float outsideU = max(0.0f, abs(u) - halfW);
        float outsideV = max(0.0f, abs(v) - halfH);
        float outsideDist = sqrt(outsideU * outsideU + outsideV * outsideV);
        if (outsideDist > 0.001f)
        {
            float falloffRange = max(halfW, halfH);
            shapeFactor = 1.0f - saturate(outsideDist / falloffRange);
            shapeFactor = shapeFactor * shapeFactor * shapeFactor;
        }

        float facingFactor = max(0.0f, dot(aL.normal, -L));
        float finalAttenuation = distAttenuation * shapeFactor * facingFactor;

        Lo += CalculatePBRLighting(N, V, L, aL.color.rgb, aL.intensity * finalAttenuation, albedo, metallic, roughness, ao);
    }

    // IBL アンビエント（PBR.hlsli CalculateFullIBL と同じロジック）
    float3 ambient = float3(0.0f, 0.0f, 0.0f);
    if (enableIBL)
    {
        ambient = CalculateDeferredIBL(N, V, albedo, metallic, roughness, F0, ao);
    }

    // PBR: ACES トーンマッピング
    float3 color = ACESFilm(Lo + ambient + emissive);
    output.color = float4(color, 1.0f);
    return output;
}
