// Object3dForward.hlsli
#include "Object3d.hlsli"
#include "ObjectMaterial.hlsli"
#include "../Lighting/LightStructures.hlsli"
#include "../Lighting/ShadowLightFloor.hlsli"
#include "../PBR/PBR.hlsli"
#include "Fog.hlsli"

// ===== カメラ =====
struct Camera
{
    float3 worldPosition;
};

// ===== ConstantBuffer =====
// gMaterial(b0) と マテリアルテクスチャ(t0, t7-t10) は ObjectMaterial.hlsli で定義
ConstantBuffer<Camera> gCamera : register(b2);
ConstantBuffer<LightCounts> gLightCounts : register(b1);
// フォグ。全画面合成パス（HeightFog.CS）と同じ数式・同じ定数を使う。
// 不透明フォワードには全画面パスが深度から掛けるので、C++ 側が「何もしない」
// バリアントを差して二重適用を防ぐ（BaseModelRenderer::BindForwardSceneResources）。
// b4 = Water.VS の WaterConstants、b5 = WaterFrameConstants、b6 = AtmosphereApply の
// gAtmosphereAP が使うため、フォワード系で空いている b7 を使う
ConstantBuffer<FogParameters> gFog : register(b7);

// ===== StructuredBuffer (Lights) =====
StructuredBuffer<DirectionalLightData> gDirectionalLights : register(t1);
StructuredBuffer<PointLightData> gPointLights : register(t2);
StructuredBuffer<SpotLightData> gSpotLights : register(t3);
StructuredBuffer<AreaLightData> gAreaLights : register(t4);

// ===== RT Shadow Mask =====
// DXRレイトレ影のスクリーン空間マスク（メインライト分・GameView解像度）。
// 旧従来型シャドウマップ gShadowMap(t6) の置き換え（2026-07-25）。
// C++側は DeferredLightingPass::Setup が毎フレーム供給し、
// マスク未提供フレームは white1x1（=影なし）がバインドされる。
Texture2D<float> gRTShadowMask : register(t6);

// ===== ライティング計算ヘルパー =====

/// @brief メインライトの RT シャドウマスクを画面の位置で読む（全ディレクショナルライト共通）
/// @details 1x1 のダミー（マスク未提供・非 DXR 環境）は「影なし」の 1 を返す。座標はマスクの範囲へクランプして読む
float LoadForwardRTShadow(float2 screenPosition)
{
    float rtW, rtH;
    gRTShadowMask.GetDimensions(rtW, rtH);
    if (rtW > 1.0f && rtH > 1.0f)
    {
        int2 coord = int2(clamp(screenPosition, float2(0.0f, 0.0f), float2(rtW - 1.0f, rtH - 1.0f)));
        return gRTShadowMask.Load(int3(coord, 0)).r;
    }
    return 1.0f;
}

/// @brief 空の環境光が無いときの代わりの環境光（ディレクショナルライトの半球ランバート）
/// @details DeferredLighting の代わりの環境光と同じく、影の中にも kShadowLightFloor だけ残す
float3 CalculateFallbackAmbient(VertexShaderOutput input, float3 albedo, float metallic, float ao)
{
    float ambientShadow = ApplyShadowLightFloor(LoadForwardRTShadow(input.position.xy));
    float3 ambient = float3(0.0f, 0.0f, 0.0f);
    for (uint i = 0; i < gLightCounts.directionalLightCount; ++i)
    {
        if (gDirectionalLights[i].enabled == 0)
            continue;
        float3 L = normalize(-gDirectionalLights[i].direction);
        ambient += CalculateHalfLambertAmbient(input.normal, L,
            gDirectionalLights[i].color.rgb, gDirectionalLights[i].intensity, albedo, metallic, ao) * ambientShadow;
    }
    return ambient;
}

/// @brief ディレクショナルライトのライティングを計算
/// @details 影の中の直接光は 0（拡散もスペキュラも）。影の中の明るさは環境光が受け持つ
void CalculateDirectionalLights(
    VertexShaderOutput input,
    float3 albedo, float metallic, float roughness, float ao, float3 toEye,
    inout float3 totalDiffuse, inout float3 totalSpecular)
{
    float rtShadow = LoadForwardRTShadow(input.position.xy);

    for (uint i = 0; i < gLightCounts.directionalLightCount; ++i)
    {
        if (gDirectionalLights[i].enabled == 0)
            continue;

        LightingResult result = CalculateDirectionalLightPBR(
            input.normal, gDirectionalLights[i].direction,
            gDirectionalLights[i].color.rgb, gDirectionalLights[i].intensity,
            toEye, albedo, metallic, roughness, ao);
        totalDiffuse += result.diffuse * rtShadow;
        totalSpecular += result.specular * rtShadow;
    }
}

/// @brief ポイントライトのライティングを計算
void CalculatePointLights(
    VertexShaderOutput input,
    float3 albedo, float metallic, float roughness, float ao, float3 toEye,
    inout float3 totalDiffuse, inout float3 totalSpecular)
{
    for (uint j = 0; j < gLightCounts.pointLightCount; ++j)
    {
        if (gPointLights[j].enabled == 0)
            continue;
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

/// @brief スポットライトのライティングを計算
void CalculateSpotLights(
    VertexShaderOutput input,
    float3 albedo, float metallic, float roughness, float ao, float3 toEye,
    inout float3 totalDiffuse, inout float3 totalSpecular)
{
    for (uint k = 0; k < gLightCounts.spotLightCount; ++k)
    {
        if (gSpotLights[k].enabled == 0)
            continue;
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

/// @brief エリアライトのライティングを計算（最近接点計算 + PBR BRDF）
void CalculateAreaLights(
    VertexShaderOutput input,
    float3 albedo, float metallic, float roughness, float ao, float3 toEye,
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

        // 物理ベース: 純粋な逆二乗則 + 範囲窓関数（Deferred と同一式。intensity は 輝度[nt]×面積 由来）
        float aDistRatio = dist / gAreaLights[l].range;
        float aRangeFactor = saturate(1.0f - aDistRatio * aDistRatio * aDistRatio * aDistRatio);
        float distAtten = aRangeFactor * aRangeFactor / max(dist * dist, 0.01f);
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

        float3 contrib = CalculatePBRLighting(
            normalize(input.normal), normalize(toEye), L,
            gAreaLights[l].color.rgb, gAreaLights[l].intensity * finalAtten,
            albedo, metallic, roughness, ao);
        totalDiffuse += contrib;
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
    float3 totalDiffuse = float3(0.0f, 0.0f, 0.0f);
    float3 totalSpecular = float3(0.0f, 0.0f, 0.0f);

    CalculateDirectionalLights(input, albedo, metallic, roughness, ao, toEye, totalDiffuse, totalSpecular);
    CalculatePointLights(input, albedo, metallic, roughness, ao, toEye, totalDiffuse, totalSpecular);
    CalculateSpotLights(input, albedo, metallic, roughness, ao, toEye, totalDiffuse, totalSpecular);
    CalculateAreaLights(input, albedo, metallic, roughness, ao, toEye, totalDiffuse);

    return totalDiffuse + totalSpecular;
}

// ===== ピクセルシェーダー出力 =====
struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};
