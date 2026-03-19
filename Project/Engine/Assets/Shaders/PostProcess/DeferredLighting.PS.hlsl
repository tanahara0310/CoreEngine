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
// IBL
// ============================================================
float3 CalculateDiffuseIBL(float3 N, float3 albedo, float metallic, float3 F0, float ao)
{
    float3 irr = gIrradianceMap.Sample(gSampler, N).rgb;
    float3 kD = (1.0f - F0) * (1.0f - metallic);
    return kD * albedo / PI * irr * ao;
}

float3 CalculateSpecularIBL(float3 N, float3 V, float roughness, float3 F0)
{
    float NdotV = max(dot(N, V), 0.0f);
    float3 R = reflect(-V, N);
    float mip = roughness * float(MAX_PREFILTERED_MIP_LEVELS - 1);
    float3 pfc = gPrefilteredMap.SampleLevel(gSampler, R, mip).rgb;
    float2 brdf = gBRDFLUT.Sample(gSampler, float2(NdotV, roughness));
    float3 F = FresnelSchlickRoughness(NdotV, F0, roughness);
    return pfc * (F * brdf.x + brdf.y);
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

    float4 albedoAO = gAlbedoAO.Sample(gSampler, input.texcoord);
    float4 normalRoughness = gNormalRoughness.Sample(gSampler, input.texcoord);
    float4 emissiveMetallic = gEmissiveMetallic.Sample(gSampler, input.texcoord);
    float4 worldPosSample = gWorldPosition.Sample(gSampler, input.texcoord);

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
    // 1 = 非PBR（従来ライティング）, 2 = PBR + IBL無効, 3 = PBR + IBL有効
    int pixelType = int(round(worldPosSample.a));

    // ===== アンリットマテリアル検出 =====
    // GBuffer.PS.hlsl は enableLighting==0 / shadingMode==0 のとき
    // normalRoughness.a=0 のセンチネル値を書き込み、emissiveMetallic.rgb にアンリットカラーを格納する。
    // Forward パスと同様にトーンマッピングを適用せずそのまま出力する。
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
    // 非PBR ライティングパス (pixelType == 1)
    // Forward パス (Object3d.PS.hlsl + Lighting.hlsli) と同じ
    // Lambert / Half-Lambert / Toon ライティングを再現する。
    // ============================================================
    if (pixelType <= 1)
    {
        // GBuffer にエンコードされた従来ライティングパラメータをデコード
        int   shadingMode   = int(round(emissiveMetallic.a * 4.0f));
        float shininess     = normalRoughness.a;   // roughness チャネルに格納
        float toonThreshold = albedoAO.a;           // AO チャネルに格納

        float3 totalDiffuse  = float3(0.0f, 0.0f, 0.0f);
        float3 totalSpecular = float3(0.0f, 0.0f, 0.0f);

        // ディレクショナルライト（Forward: CalculateDirectionalLight と同じ）
        for (uint di = 0; di < gLightCounts.directionalLightCount; ++di)
        {
            DirectionalLightData dL = gDirectionalLights[di];
            if (!dL.enabled)
                continue;
            float3 L    = normalize(-dL.direction);
            float NdotL = dot(N, L);
            float diff  = CalculateDiffuseFactor(NdotL, shadingMode, toonThreshold);
            float spec  = CalculateSpecularFactor(N, L, V, shininess, shadingMode);

            // Forward と同じシャドウ適用：diffuse にのみ乗算
            float shadowFactor = CalculateShadow(lightSpacePos, N, dL.direction, gShadowMap, gShadowSampler);
            shadowFactor = lerp(0.3f, 1.0f, shadowFactor);

            totalDiffuse  += albedo * dL.color.rgb * diff * dL.intensity * shadowFactor;
            totalSpecular += dL.color.rgb * dL.intensity * spec;
        }

        // ポイントライト（Forward: CalculatePointLight と同じ pow 減衰）
        for (uint pi = 0; pi < gLightCounts.pointLightCount; ++pi)
        {
            PointLightData pL = gPointLights[pi];
            if (!pL.enabled)
                continue;
            float3 tv = pL.position - worldPos;
            float  d  = length(tv);
            if (d >= pL.radius)
                continue;
            float3 L          = normalize(tv);
            float attenuation = pow(saturate(1.0f - d / pL.radius), pL.decay);
            float NdotL       = dot(N, L);
            float diff        = CalculateDiffuseFactor(NdotL, shadingMode, toonThreshold);
            float spec        = CalculateSpecularFactor(N, L, V, shininess, shadingMode);

            totalDiffuse  += albedo * pL.color.rgb * diff * pL.intensity * attenuation;
            totalSpecular += pL.color.rgb * pL.intensity * spec * attenuation;
        }

        // スポットライト（Forward: CalculateSpotLight と同じ pow 減衰 + コーン）
        for (uint si = 0; si < gLightCounts.spotLightCount; ++si)
        {
            SpotLightData sL = gSpotLights[si];
            if (!sL.enabled)
                continue;
            float3 tv = sL.position - worldPos;
            float  d  = length(tv);
            if (d >= sL.distance)
                continue;
            float3 L                   = normalize(tv);
            float3 spotLightDirOnSurf  = -L;
            float  cosAngleValue       = dot(spotLightDirOnSurf, sL.direction);
            float  denominator         = max(sL.cosAngle - sL.cosFalloffStart, 0.001f);
            float  falloffFactor       = saturate((cosAngleValue - sL.cosAngle) / denominator);
            float  distanceAttenuation = pow(saturate(1.0f - d / sL.distance), sL.decay);
            float  attenuation         = distanceAttenuation * falloffFactor;

            float NdotL = dot(N, L);
            float diff  = CalculateDiffuseFactor(NdotL, shadingMode, toonThreshold);
            float spec  = CalculateSpecularFactor(N, L, V, shininess, shadingMode);

            totalDiffuse  += albedo * sL.color.rgb * diff * sL.intensity * attenuation;
            totalSpecular += sL.color.rgb * sL.intensity * spec * attenuation;
        }

        // エリアライト（簡易版：距離ベース減衰 + 従来ライティング）
        for (uint ai = 0; ai < gLightCounts.areaLightCount; ++ai)
        {
            AreaLightData aL = gAreaLights[ai];
            if (!aL.enabled)
                continue;
            float3 tv = aL.position - worldPos;
            float  d  = length(tv);
            if (d >= aL.range)
                continue;
            float3 L          = normalize(tv);
            float  distFactor = 1.0f - saturate(d / aL.range);
            float  attenuation = distFactor * distFactor;

            float NdotL = dot(N, L);
            float diff  = CalculateDiffuseFactor(NdotL, shadingMode, toonThreshold);
            float spec  = CalculateSpecularFactor(N, L, V, shininess, shadingMode);

            totalDiffuse  += albedo * aL.color.rgb * diff * aL.intensity * attenuation;
            totalSpecular += aL.color.rgb * aL.intensity * spec * attenuation;
        }

        // 非PBR: Forward と同様 ACES トーンマッピングなし
        output.color = float4(totalDiffuse + totalSpecular, 1.0f);
        return output;
    }

    // ============================================================
    // PBR ライティングパス (pixelType == 2 or 3)
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

    // エリアライト
    for (uint ai = 0; ai < gLightCounts.areaLightCount; ++ai)
    {
        AreaLightData aL = gAreaLights[ai];
        if (!aL.enabled)
            continue;
        float3 tv = aL.position - worldPos;
        float d = length(tv);
        if (d >= aL.range)
            continue;
        float3 L = normalize(tv);
        float a = max(0.0f, 1.0f - d / aL.range);
        Lo += CalculatePBRLighting(N, V, L, aL.color.rgb, aL.intensity * a, albedo, metallic, roughness, ao);
    }

    // IBL アンビエント
    float3 ambient = float3(0.0f, 0.0f, 0.0f);
    if (enableIBL)
    {
        ambient = CalculateDiffuseIBL(N, albedo, metallic, F0, ao)
                + CalculateSpecularIBL(N, V, roughness, F0) * ao;
    }

    // PBR: ACES トーンマッピング
    float3 color = ACESFilm(Lo + ambient + emissive);
    output.color = float4(color, 1.0f);
    return output;
}
