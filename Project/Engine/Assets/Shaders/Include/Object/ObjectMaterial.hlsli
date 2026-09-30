// ObjectMaterial.hlsli
// モデル描画（Forward / GBuffer）共通のマテリアル定義とサンプリングヘルパー。
// CPU 側 MaterialConstants (Graphics/Material/MaterialConstants.h) と
// メモリレイアウトを一致させること。
#ifndef OBJECT_MATERIAL_HLSLI
#define OBJECT_MATERIAL_HLSLI

// ===== マテリアル =====
// 定数（struct Material / gMaterial）は頂点シェーダーとも共有するため別ファイルに置いている
#include "ObjectMaterialConstants.hlsli"

// ===== マテリアルテクスチャ & サンプラー =====
Texture2D<float4> gTexture : register(t0); // ベースカラー
SamplerState gSampler : register(s0);

Texture2D<float4> gNormalMap : register(t7); // 法線マップ (RGB: タンジェント空間法線)
Texture2D<float4> gMetallicRoughnessMap : register(t8); // glTF MetallicRoughness (G: 粗さ, B: 金属性)
Texture2D<float4> gEmissiveMap : register(t9); // エミッシブマップ
Texture2D<float> gAOMap : register(t10); // AOマップ (R: 環境遮蔽)

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

/// @brief アルファカット判定（Forward / GBuffer 共通）
/// @param finalAlpha gMaterial.color.a * テクスチャアルファ
/// @param screenPos SV_POSITION.xy
/// @return true なら discard すべき
bool ShouldDiscardByAlpha(float finalAlpha, float2 screenPos)
{
    if (gMaterial.enableDithering != 0)
    {
        if (gMaterial.ditheringScale > 0.0f)
        {
            screenPos *= gMaterial.ditheringScale;
        }
        return finalAlpha <= GetDitheringThreshold(screenPos) + 0.001f;
    }
    return finalAlpha <= gMaterial.alphaCutoff;
}

/// @brief PBR パラメータを取得（ファクター × テクスチャの乗算合成）
/// glTF 仕様: MetallicRoughness テクスチャは G=Roughness, B=Metallic
void GetPBRParameters(float2 uv, out float outMetallic, out float outRoughness, out float outAO)
{
    float4 mr = gMetallicRoughnessMap.Sample(gSampler, uv);
    outMetallic = gMaterial.metallic * mr.b;
    outRoughness = gMaterial.roughness * mr.g;

    float occlusion = gAOMap.Sample(gSampler, uv);
    outAO = lerp(1.0f, occlusion, gMaterial.occlusionStrength);
}

/// @brief エミッシブカラーを取得（ファクター × テクスチャ）
float3 GetEmissive(float2 uv)
{
    return gMaterial.emissiveFactor * gEmissiveMap.Sample(gSampler, uv).rgb;
}

/// @brief ノーマルマップから法線を取得してワールド空間に変換
float3 GetNormalFromMap(float3 vertexNormal, float3 vertexTangent, float3 vertexBitangent, float2 uv)
{
    if (gMaterial.useNormalMap == 0)
        return normalize(vertexNormal);

    float3 normalMapSample = gNormalMap.Sample(gSampler, uv).rgb;
    float3 tangentSpaceNormal = normalMapSample * 2.0f - 1.0f;

    float3 N = normalize(vertexNormal);
    float3 T = normalize(vertexTangent);
    float3 B = normalize(vertexBitangent);

    T = normalize(T - dot(T, N) * N);
    B = cross(N, T);

    float3x3 TBN = float3x3(T, B, N);
    return normalize(mul(tangentSpaceNormal, TBN));
}

#endif // OBJECT_MATERIAL_HLSLI
