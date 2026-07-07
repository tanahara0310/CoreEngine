// Object3dForward.hlsli
#include "Object3d.hlsli"
#include "../Lighting/LightStructures.hlsli"
#include "../Shadow/ShadowCalculation.hlsli"
#include "../PBR/PBR.hlsli"

// ===== マテリアル =====
struct Material
{
    float4 color;
    int enableLighting;
    float4x4 uvTransform;

    // ===== PBR Parameters =====
    float metallic;
    float roughness;
    float ao;
    int useNormalMap;
    int useMetallicMap;
    int useRoughnessMap;
    int useAOMap;

    // ===== Alpha =====
    int enableDithering;
    float ditheringScale;

    // ===== Shading Mode =====
    int shadingMode; ///< 0=PBR, 1=PBR+IBL, 2=Lambert, 3=HalfLambert
    float iblIntensity;
    float alphaCutoff; ///< discard 判定に使用するアルファしきい値
};

/// @brief シーン共通 IBL パラメータ（スカイボックス回転と連動）
struct IBLSceneParams
{
    float3 environmentRotation; ///< XYZ 環境回転（ラジアン）
    float environmentIntensity; ///< 環境輝度スケール（SkyBox intensity と連動）
};

// ===== カメラ =====
struct Camera
{
    float3 worldPosition;
};

// ===== ConstantBuffer =====
ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<Camera> gCamera : register(b2);
ConstantBuffer<LightCounts> gLightCounts : register(b1);
ConstantBuffer<IBLSceneParams> gIBLParams : register(b3);

// ===== Texture & Sampler =====
Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

// ===== StructuredBuffer (Lights) =====
StructuredBuffer<DirectionalLightData> gDirectionalLights : register(t1);
StructuredBuffer<PointLightData> gPointLights : register(t2);
StructuredBuffer<SpotLightData> gSpotLights : register(t3);
StructuredBuffer<AreaLightData> gAreaLights : register(t4);

// ===== Shadow Map =====
Texture2D<float> gShadowMap : register(t6);
SamplerComparisonState gShadowSampler : register(s1);

// ===== PBR Texture Maps =====
Texture2D<float4> gNormalMap : register(t7); // 法線マップ (RGB: タンジェント空間法線)
Texture2D<float> gMetallicMap : register(t8); // メタリックマップ (R: 金属性)
Texture2D<float> gRoughnessMap : register(t9); // ラフネスマップ (R: 粗さ)
Texture2D<float> gAOMap : register(t10); // AOマップ (R: 環境遮蔽)

// ===== IBL Texture Maps =====
TextureCube<float4> gIrradianceMap : register(t11); // Irradianceマップ（拡散IBL）
TextureCube<float4> gPrefilteredMap : register(t12); // Prefilteredマップ（スペキュラIBL）
Texture2D<float2> gBRDFLUT : register(t13); // BRDF LUT（スペキュラIBL統合用）

// ===== ディザリングパターン（4x4 Bayer Matrix）=====
float GetDitheringThreshold(float2 screenPos)
{
    const float bayerMatrix[4][4] =
    {
        { 0.0f / 16.0f, 8.0f / 16.0f, 2.0f / 16.0f, 10.0f / 16.0f },
        { 12.0f / 16.0f, 4.0f / 16.0f, 14.0f / 16.0f, 6.0f / 16.0f },
        { 3.0f / 16.0f, 11.0f / 16.0f, 1.0f / 16.0f, 9.0f / 16.0f },
        { 15.0f / 16.0f, 7.0f / 16.0f, 13.0f / 16.0f, 5.0f / 16.0f }
    };
    int x = int(screenPos.x) % 4;
    int y = int(screenPos.y) % 4;
    return bayerMatrix[y][x];
}

/// @brief PBR パラメータを取得（テクスチャマップまたはマテリアル値から）
void GetPBRParameters(float2 uv, out float outMetallic, out float outRoughness, out float outAO)
{
    outMetallic = gMaterial.useMetallicMap != 0 ? gMetallicMap.Sample(gSampler, uv) : gMaterial.metallic;
    outRoughness = gMaterial.useRoughnessMap != 0 ? gRoughnessMap.Sample(gSampler, uv) : gMaterial.roughness;
    outAO = gMaterial.useAOMap != 0 ? gAOMap.Sample(gSampler, uv) : gMaterial.ao;
}

/// @brief ノーマルマップから法線を取得してワールド空間に変換
float3 GetNormalFromMap(VertexShaderOutput input, float2 uv)
{
    if (gMaterial.useNormalMap == 0)
        return normalize(input.normal);

    float3 normalMapSample = gNormalMap.Sample(gSampler, uv).rgb;
    float3 tangentSpaceNormal = normalMapSample * 2.0f - 1.0f;

    float3 N = normalize(input.normal);
    float3 T = normalize(input.tangent);
    float3 B = normalize(input.bitangent);

    T = normalize(T - dot(T, N) * N);
    B = cross(N, T);

    float3x3 TBN = float3x3(T, B, N);
    return normalize(mul(tangentSpaceNormal, TBN));
}

// ===== ライティング計算ヘルパー =====

/// @brief ディレクショナルライトのライティングを計算
void CalculateDirectionalLights(
    VertexShaderOutput input,
    float3 albedo, float metallic, float roughness, float ao, float3 toEye,
    bool useLambert, bool useTraditional,
    inout float3 totalDiffuse, inout float3 totalSpecular)
{
    for (uint i = 0; i < gLightCounts.directionalLightCount; ++i)
    {
        if (gDirectionalLights[i].enabled == 0)
            continue;
        float3 L = normalize(-gDirectionalLights[i].direction);
        float shadowFactor = CalculateShadow(
            input.lightSpacePos, input.normal,
            gDirectionalLights[i].direction,
            gShadowMap, gShadowSampler);
        shadowFactor = lerp(0.3f, 1.0f, shadowFactor);

        if (useTraditional)
        {
            float3 diff = useLambert
                ? CalculateLambertDiffuse(input.normal, L, gDirectionalLights[i].color.rgb, gDirectionalLights[i].intensity, albedo, ao)
                : CalculateHalfLambertDiffuse(input.normal, L, gDirectionalLights[i].color.rgb, gDirectionalLights[i].intensity, albedo, ao);
            totalDiffuse += diff * shadowFactor;
        }
        else
        {
            LightingResult result = CalculateDirectionalLightPBR(
                input.normal, gDirectionalLights[i].direction,
                gDirectionalLights[i].color.rgb, gDirectionalLights[i].intensity,
                toEye, albedo, metallic, roughness, ao);
            totalDiffuse += result.diffuse * shadowFactor;
            totalSpecular += result.specular;
        }
    }
}

/// @brief ポイントライトのライティングを計算
void CalculatePointLights(
    VertexShaderOutput input,
    float3 albedo, float metallic, float roughness, float ao, float3 toEye,
    bool useLambert, bool useTraditional,
    inout float3 totalDiffuse, inout float3 totalSpecular)
{
    for (uint j = 0; j < gLightCounts.pointLightCount; ++j)
    {
        if (gPointLights[j].enabled == 0)
            continue;
        if (useTraditional)
        {
            float3 tv = gPointLights[j].position - input.worldPosition;
            float d = length(tv);
            if (d >= gPointLights[j].radius)
                continue;
            float3 L = normalize(tv);
            float atten = 1.0f / (1.0f + gPointLights[j].decay * d * d);
            float pRatio = d / gPointLights[j].radius;
            float pRange = saturate(1.0f - pRatio * pRatio * pRatio * pRatio);
            atten *= pRange * pRange;
            float3 diff = useLambert
                ? CalculateLambertDiffuse(input.normal, L, gPointLights[j].color.rgb, gPointLights[j].intensity * atten, albedo, ao)
                : CalculateHalfLambertDiffuse(input.normal, L, gPointLights[j].color.rgb, gPointLights[j].intensity * atten, albedo, ao);
            totalDiffuse += diff;
        }
        else
        {
            LightingResult result = CalculatePointLightPBR(
                input.normal,
                gPointLights[j].position, input.worldPosition,
                gPointLights[j].color.rgb, gPointLights[j].intensity,
                gPointLights[j].radius, gPointLights[j].decay,
                toEye, albedo, metallic, roughness, ao);
            totalDiffuse += result.diffuse;
            totalSpecular += result.specular;
        }
    }
}

/// @brief スポットライトのライティングを計算
void CalculateSpotLights(
    VertexShaderOutput input,
    float3 albedo, float metallic, float roughness, float ao, float3 toEye,
    bool useLambert, bool useTraditional,
    inout float3 totalDiffuse, inout float3 totalSpecular)
{
    for (uint k = 0; k < gLightCounts.spotLightCount; ++k)
    {
        if (gSpotLights[k].enabled == 0)
            continue;
        if (useTraditional)
        {
            float3 tv = gSpotLights[k].position - input.worldPosition;
            float d = length(tv);
            if (d >= gSpotLights[k].distance)
                continue;
            float3 L = normalize(tv);
            float atten = 1.0f / (1.0f + gSpotLights[k].decay * d * d);
            float sRatio = d / gSpotLights[k].distance;
            float sRange = saturate(1.0f - sRatio * sRatio * sRatio * sRatio);
            atten *= sRange * sRange;
            float cosTheta = dot(-L, normalize(gSpotLights[k].direction));
            if (cosTheta < gSpotLights[k].cosAngle)
                continue;
            atten *= saturate((cosTheta - gSpotLights[k].cosAngle) / (gSpotLights[k].cosFalloffStart - gSpotLights[k].cosAngle));
            float3 diff = useLambert
                ? CalculateLambertDiffuse(input.normal, L, gSpotLights[k].color.rgb, gSpotLights[k].intensity * atten, albedo, ao)
                : CalculateHalfLambertDiffuse(input.normal, L, gSpotLights[k].color.rgb, gSpotLights[k].intensity * atten, albedo, ao);
            totalDiffuse += diff;
        }
        else
        {
            LightingResult result = CalculateSpotLightPBR(
                input.normal,
                gSpotLights[k].position, gSpotLights[k].direction, input.worldPosition,
                gSpotLights[k].color.rgb, gSpotLights[k].intensity,
                gSpotLights[k].distance, gSpotLights[k].decay,
                gSpotLights[k].cosAngle, gSpotLights[k].cosFalloffStart,
                toEye, albedo, metallic, roughness, ao);
            totalDiffuse += result.diffuse;
            totalSpecular += result.specular;
        }
    }
}

/// @brief エリアライトのライティングを計算（最近接点計算 + PBR BRDF）
void CalculateAreaLights(
    VertexShaderOutput input,
    float3 albedo, float metallic, float roughness, float ao, float3 toEye,
    bool useLambert, bool useTraditional,
    inout float3 totalDiffuse)
{
    for (uint l = 0; l < gLightCounts.areaLightCount; ++l)
    {
        if (gAreaLights[l].enabled == 0)
            continue;
        float3 toLight = gAreaLights[l].position - input.worldPosition;
        float distToPlane = dot(toLight, gAreaLights[l].normal);
        float3 projPoint = input.worldPosition + gAreaLights[l].normal * distToPlane;
        float3 offset = projPoint - gAreaLights[l].position;
        float u = dot(offset, gAreaLights[l].right);
        float v = dot(offset, gAreaLights[l].up);
        float halfW = gAreaLights[l].width * 0.5f;
        float halfH = gAreaLights[l].height * 0.5f;

        float3 closest = gAreaLights[l].position
                       + gAreaLights[l].right * clamp(u, -halfW, halfW)
                       + gAreaLights[l].up * clamp(v, -halfH, halfH);

        float3 toClosest = closest - input.worldPosition;
        float dist = length(toClosest);
        if (dist >= gAreaLights[l].range)
            continue;
        float3 L = toClosest / max(dist, 0.001f);

        float distFactor = 1.0f - saturate(dist / gAreaLights[l].range);
        float distAtten = distFactor * distFactor;
        float outsideU = max(0.0f, abs(u) - halfW);
        float outsideV = max(0.0f, abs(v) - halfH);
        float outsideDist = sqrt(outsideU * outsideU + outsideV * outsideV);
        float shapeFactor = 1.0f;
        if (outsideDist > 0.001f)
        {
            float falloff = max(halfW, halfH);
            shapeFactor = 1.0f - saturate(outsideDist / falloff);
            shapeFactor = shapeFactor * shapeFactor * shapeFactor;
        }
        float facingFactor = max(0.0f, dot(gAreaLights[l].normal, -L));
        float finalAtten = distAtten * shapeFactor * facingFactor;

        if (useTraditional)
        {
            float3 diff = useLambert
                ? CalculateLambertDiffuse(input.normal, L, gAreaLights[l].color.rgb, gAreaLights[l].intensity * finalAtten, albedo, ao)
                : CalculateHalfLambertDiffuse(input.normal, L, gAreaLights[l].color.rgb, gAreaLights[l].intensity * finalAtten, albedo, ao);
            totalDiffuse += diff;
        }
        else
        {
            float3 contrib = CalculatePBRLighting(
                normalize(input.normal), normalize(toEye), L,
                gAreaLights[l].color.rgb, gAreaLights[l].intensity * finalAtten,
                albedo, metallic, roughness, ao);
            totalDiffuse += contrib;
        }
    }
}

/// @brief 全ライトの PBR ライティングを計算
float3 CalculateAllLighting(
    VertexShaderOutput input,
    float3 albedo,
    float metallic,
    float roughness,
    float ao,
    float3 toEye,
    float4 textureColor)
{
    const bool useLambert = (gMaterial.shadingMode == 2);
    const bool useHalfLambert = (gMaterial.shadingMode == 3);
    const bool useTraditional = useLambert || useHalfLambert;

    float3 totalDiffuse = float3(0.0f, 0.0f, 0.0f);
    float3 totalSpecular = float3(0.0f, 0.0f, 0.0f);

    CalculateDirectionalLights(input, albedo, metallic, roughness, ao, toEye, useLambert, useTraditional, totalDiffuse, totalSpecular);
    CalculatePointLights(input, albedo, metallic, roughness, ao, toEye, useLambert, useTraditional, totalDiffuse, totalSpecular);
    CalculateSpotLights(input, albedo, metallic, roughness, ao, toEye, useLambert, useTraditional, totalDiffuse, totalSpecular);
    CalculateAreaLights(input, albedo, metallic, roughness, ao, toEye, useLambert, useTraditional, totalDiffuse);

    return totalDiffuse + totalSpecular;
}

/// @brief IBL（Image-Based Lighting）を適用
float3 ApplyIBL(
    VertexShaderOutput input,
    float3 albedo,
    float metallic,
    float roughness,
    float ao,
    float3 toEye)
{
    if (gMaterial.shadingMode != 1)
        return float3(0.0f, 0.0f, 0.0f);

    float3 iblColor = CalculateFullIBL(
        normalize(input.normal), normalize(toEye),
        albedo, metallic, roughness, ao,
        gIrradianceMap, gPrefilteredMap, gBRDFLUT,
        gSampler, gIBLParams.environmentRotation);

    return iblColor * gMaterial.iblIntensity * gIBLParams.environmentIntensity;
}

// ===== ピクセルシェーダー出力 =====
struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

/// @brief Forward パス共通の描画処理ヘルパー（main から呼ぶ）
PixelShaderOutput ForwardMain(VertexShaderOutput input)
{
    PixelShaderOutput output;

    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float4 textureColor  = gTexture.Sample(gSampler, transformedUV.xy);

    float3 finalNormal = GetNormalFromMap(input, transformedUV.xy);
    input.normal = finalNormal;

    float3 toEye     = normalize(gCamera.worldPosition - input.worldPosition);
    float  finalAlpha = gMaterial.color.a * textureColor.a;

    // アルファカット（ディザリング or 通常テスト）
    if (gMaterial.enableDithering != 0)
    {
        float2 screenPos = input.position.xy;
        if (gMaterial.ditheringScale > 0.0f)
            screenPos *= gMaterial.ditheringScale;
        if (finalAlpha <= GetDitheringThreshold(screenPos) + 0.001f)
            discard;
    }
    else if (finalAlpha <= gMaterial.alphaCutoff)
    {
        discard;
    }

    // アンリット
    if (gMaterial.enableLighting == 0)
    {
        output.color = gMaterial.color * textureColor;
        return output;
    }

    // PBR パラメータ取得
    float metallic, roughness, ao;
    GetPBRParameters(transformedUV.xy, metallic, roughness, ao);
    roughness = max(roughness, 0.01f);

    float3 albedo = gMaterial.color.rgb * textureColor.rgb;

    // PBR ライティング
    output.color.rgb = CalculateAllLighting(input, albedo, metallic, roughness, ao, toEye, textureColor);
    output.color.a   = finalAlpha;

    // IBL
    output.color.rgb += ApplyIBL(input, albedo, metallic, roughness, ao, toEye);

    // HDR 値をそのまま出力（トーンマッピングはポストエフェクトチェーンで適用）

    // ディザリング時はアルファを 1.0 に固定
    if (gMaterial.enableDithering != 0)
        output.color.a = 1.0f;

    return output;
}
