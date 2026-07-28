// Object3dForward.hlsli
#include "Object3d.hlsli"
#include "ObjectMaterial.hlsli"
#include "../Lighting/LightStructures.hlsli"
#include "../PBR/PBR.hlsli"

/// @brief シーン共通 IBL パラメータ（スカイボックス回転と連動）
struct IBLSceneParams
{
    float3 environmentRotation; ///< XYZ 環境回転（ラジアン）
    float environmentIntensity; ///< 環境輝度スケール（SkyBox intensity と連動）
    int sceneIBLEnabled; ///< シーンに IBL マップ（Irradiance/Prefiltered/BRDF LUT）が揃っているか
    float3 padding;
};

// ===== カメラ =====
struct Camera
{
    float3 worldPosition;
};

// ===== ConstantBuffer =====
// gMaterial(b0) と マテリアルテクスチャ(t0, t7-t10) は ObjectMaterial.hlsli で定義
ConstantBuffer<Camera> gCamera : register(b2);
ConstantBuffer<LightCounts> gLightCounts : register(b1);
ConstantBuffer<IBLSceneParams> gIBLParams : register(b3);

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

// ===== IBL Texture Maps =====
TextureCube<float4> gIrradianceMap : register(t11); // Irradianceマップ（拡散IBL）
TextureCube<float4> gPrefilteredMap : register(t12); // Prefilteredマップ（スペキュラIBL）
Texture2D<float2> gBRDFLUT : register(t13); // BRDF LUT（スペキュラIBL統合用）

// ===== ライティング計算ヘルパー =====

/// @brief ディレクショナルライトのライティングを計算
void CalculateDirectionalLights(
    VertexShaderOutput input,
    float3 albedo, float metallic, float roughness, float ao, float3 toEye,
    inout float3 totalDiffuse, inout float3 totalSpecular)
{
    // RTシャドウマスクをスクリーン座標でLoadする（全ディレクショナルライト共通。
    // 旧シャドウマップも単一マップを全ライトへ適用していたため意味は等価）。
    // 1x1ダミー（マスク未提供・非DXR環境）は寸法ガードで「影なし」に倒す。
    // 座標クランプは補助ビュー等でマスク解像度と画面解像度がずれた場合の
    // 範囲外Load（=0が返り誤って影られる）防止。
    float rtShadow = 1.0f;
    {
        float rtW, rtH;
        gRTShadowMask.GetDimensions(rtW, rtH);
        if (rtW > 1.0f && rtH > 1.0f)
        {
            int2 coord = int2(clamp(input.position.xy, float2(0.0f, 0.0f), float2(rtW - 1.0f, rtH - 1.0f)));
            rtShadow = gRTShadowMask.Load(int3(coord, 0)).r;
        }
    }
    float shadowFactor = lerp(0.3f, 1.0f, rtShadow);

    for (uint i = 0; i < gLightCounts.directionalLightCount; ++i)
    {
        if (gDirectionalLights[i].enabled == 0)
            continue;

        LightingResult result = CalculateDirectionalLightPBR(
            input.normal, gDirectionalLights[i].direction,
            gDirectionalLights[i].color.rgb, gDirectionalLights[i].intensity,
            toEye, albedo, metallic, roughness, ao);
        totalDiffuse += result.diffuse * shadowFactor;
        totalSpecular += result.specular;
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

/// @brief IBL（Image-Based Lighting）を適用
/// シーンに IBL マップが揃っており、かつマテリアルの iblIntensity > 0 の場合のみ寄与する
float3 ApplyIBL(
    VertexShaderOutput input,
    float3 albedo,
    float metallic,
    float roughness,
    float ao,
    float3 toEye)
{
    if (gIBLParams.sceneIBLEnabled == 0 || gMaterial.iblIntensity <= 0.0f)
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
    float2 uv = transformedUV.xy;
    float4 textureColor = gTexture.Sample(gSampler, uv);

    float3 finalNormal = GetNormalFromMap(input.normal, input.tangent, input.bitangent, uv);
    input.normal = finalNormal;

    float3 toEye     = normalize(gCamera.worldPosition - input.worldPosition);
    float  finalAlpha = gMaterial.color.a * textureColor.a;

    // アルファカット（ディザリング or 通常テスト）— GBuffer パスと共通判定
    if (ShouldDiscardByAlpha(finalAlpha, input.position.xy))
    {
        discard;
    }

    // アンリット
    if (gMaterial.enableLighting == 0)
    {
        output.color = gMaterial.color * textureColor;
        return output;
    }

    // PBR パラメータ取得（ファクター × テクスチャ）
    float metallic, roughness, ao;
    GetPBRParameters(uv, metallic, roughness, ao);
    roughness = max(roughness, 0.01f);

    float3 albedo = gMaterial.color.rgb * textureColor.rgb;

    // PBR ライティング
    output.color.rgb = CalculateAllLighting(input, albedo, metallic, roughness, ao, toEye, textureColor);
    output.color.a   = finalAlpha;

    // IBL
    output.color.rgb += ApplyIBL(input, albedo, metallic, roughness, ao, toEye);

    // エミッシブ（自己発光: ライティングの影響を受けず加算）
    output.color.rgb += GetEmissive(uv);

    // HDR 値をそのまま出力（トーンマッピングはポストエフェクトチェーンで適用）

    // ディザリング時はアルファを 1.0 に固定
    if (gMaterial.enableDithering != 0)
        output.color.a = 1.0f;

    return output;
}
