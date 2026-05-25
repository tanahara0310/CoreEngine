#include "../../../../Engine/Assets/Shaders/Include/Object/Object3dForward.hlsli"

// ===== 反射テクスチャ（Planar Reflection RTT）=====
Texture2D<float4> gReflectionTexture : register(t14);
SamplerState      gLinearClamp       : register(s2);

// ===== シーン深度テクスチャ（GBuffer 深度 / Depth Stencil SRV）=====
// WaterPlaneObject::BindCustomResources() が t15 にバインドする
// Depth Fade（Beer-Lambert）で水柱の厚さを計算するために使用する
Texture2D<float>  gSceneDepth        : register(t15);

// ===== フレーム定数バッファ（VS と共有）=====
cbuffer WaterFrameConstants : register(b5)
{
    float4 gClipPlane;
    int    gClipEnabled;
    int    gReflectionEnabled;  // 1 = 反射テクスチャ有効，0 = IBL フォールバック
    float  gFresnelMinAlpha;    // Fresnel=0（真上）のときの alpha（透明側）
    float  gFresnelMaxAlpha;    // Fresnel=1（斜め）のときの alpha（不透明側）

    // ---- Depth Fade ----
    float  gAbsorptionCoeff;    // 光吸収係数（大きいほど短距離で不透明）
    int    gDepthFadeEnabled;   // 1 = Depth Fade 有効
    float2 gFramePad;

    // ---- 水色 ----
    float3 gShallowColor;       // 浅瀬の水色
    float  gShallowColorPad;
    float3 gDeepColor;          // 深場の水色
    float  gDeepColorPad;
};

/// @brief NDC 深度値をビュー空間線形深度（メートル単位）に変換する
/// @param ndcDepth  深度テクスチャから読んだ NDC 深度 [0,1]
/// @param nearZ     ニアクリップ距離
/// @param farZ      ファークリップ距離
float LinearizeDepth(float ndcDepth, float nearZ, float farZ)
{
    // D3D12 のデプスは [0, 1]（0 = near, 1 = far）
    // NDC → ビュー空間深度に変換する
    return (nearZ * farZ) / (farZ - ndcDepth * (farZ - nearZ));
}

/// @brief Schlick 近似による Fresnel 係数を計算する
/// @param cosTheta  視線と法線のなす角の余弦（saturate 済み推奨）
/// @param f0        法線入射時の反射率（水面 ≈ 0.02）
float FresnelSchlick(float cosTheta, float f0)
{
    return f0 + (1.0f - f0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

/// @brief 水面専用フォワードパス処理（ForwardMain の discard 処理を削除したバージョン）
/// 水面は常に描画され、alpha 値で透明度を制御する
PixelShaderOutput WaterForwardMain(VertexShaderOutput input)
{
    PixelShaderOutput output;

    // UV 変換
    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float4 textureColor  = gTexture.Sample(gSampler, transformedUV.xy);

    // 法線マップ適用
    float3 finalNormal = GetNormalFromMap(input, transformedUV.xy);
    input.normal = finalNormal;

    // 視線方向と alpha
    float3 toEye     = normalize(gCamera.worldPosition - input.worldPosition);
    float  finalAlpha = gMaterial.color.a * textureColor.a;

    // ★ 水面専用: discard 処理を削除
    // 通常の ForwardMain では finalAlpha <= 0.5f で discard されるが、
    // 水面は常に描画して透明度で制御するため discard しない

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

    return output;
}

PixelShaderOutput main(VertexShaderOutput input)
{
    // ---- 1. 水面専用 PBR フォワード出力をベースにする（discard なし）----
    PixelShaderOutput output = WaterForwardMain(input);

    // ---- 2. スクリーン UV を計算する ----
    float2 screenUV = input.clipPosCurrent.xy / input.clipPosCurrent.w;
    screenUV = screenUV * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);
    screenUV = saturate(screenUV);

    // ---- 3. Depth Fade（Beer-Lambert 則）----
    // GBuffer 深度テクスチャから背後のオブジェクトの深度を取得し、
    // 水面との深度差（水柱の厚さ）を使って吸収率を計算する
    float depthFade = 0.0f;
    if (gDepthFadeEnabled)
    {
        // near/far クリップ（カメラデフォルト値）
        const float kNear = 0.1f;
        const float kFar  = 1000.0f;

        // シーン深度を NDC → ビュー空間線形深度（m）に変換する
        float sceneDepthNDC  = gSceneDepth.Sample(gLinearClamp, screenUV).r;
        float sceneDepthView = LinearizeDepth(sceneDepthNDC, kNear, kFar);

        // 水面の NDC 深度 → ビュー空間線形深度（m）に変換する
        float waterDepthNDC  = saturate(input.clipPosCurrent.z / input.clipPosCurrent.w);
        float waterDepthView = LinearizeDepth(waterDepthNDC, kNear, kFar);

        // 水柱の厚さ（m）= 背後オブジェクトの深度 - 水面の深度
        // 正値のみ使用（水面より前のオブジェクトは無視）
        float waterColumn = max(0.0f, sceneDepthView - waterDepthView);

        // Beer-Lambert 則: 深いほど光が吸収される
        depthFade = 1.0f - exp(-waterColumn * gAbsorptionCoeff);

        // 浅瀬 → 深場の水色をブレンドする
        // depthFade が 0 に近い浅瀬でも色が視認できるよう、最低ブレンド量を持たせる
        float3 waterColor = lerp(gShallowColor, gDeepColor, saturate(depthFade));
        float colorBlend  = lerp(0.25f, 0.85f, saturate(depthFade));
        output.color.rgb  = lerp(output.color.rgb, waterColor, colorBlend);
    }

    // ---- 4. 視線方向と Fresnel 係数を計算する ----
    float3 viewDir    = normalize(gCamera.worldPosition - input.worldPosition);
    float3 geomNormal = normalize(float3(0.0f, 1.0f, 0.0f));
    float  cosTheta   = saturate(dot(geomNormal, viewDir));
    float  fresnel    = FresnelSchlick(cosTheta, 0.02f);

    // ---- 5. Fresnel で alpha を制御する ----
    // 真上から見るほど透明（水中が見える）、斜めから見るほど不透明（反射が強い）
    // Depth Fade が大きい（深い）ときは浅い視線角でも不透明にする
    float fresnelAlpha = lerp(gFresnelMinAlpha, gFresnelMaxAlpha, fresnel);
    output.color.a     = max(fresnelAlpha, depthFade * gFresnelMaxAlpha);

    // ---- 6. 反射色をブレンドする ----
    if (gReflectionEnabled)
    {
        float3 reflectColor = gReflectionTexture.Sample(gLinearClamp, screenUV).rgb;
        output.color.rgb = lerp(output.color.rgb, reflectColor, fresnel * 0.7f);
    }

    return output;
}
