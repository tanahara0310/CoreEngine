#include "../../../../Engine/Assets/Shaders/Include/Object/Object3dForward.hlsli"

// ===== 反射テクスチャ（Planar Reflection RTT）=====
Texture2D<float4> gReflectionTexture : register(t14);
SamplerState gLinearClamp : register(s2);

// ===== シーン深度テクスチャ（GBuffer 深度 / Depth Stencil SRV）=====
// WaterPlaneObject::BindCustomResources() が t15 にバインドする
// Depth Fade（Beer-Lambert）で水柱の厚さを計算するために使用する
Texture2D<float> gSceneDepth : register(t15);

// ===== シーンカラーテクスチャ（OffScreen Color SRV）=====
// WaterPlaneObject::BindCustomResources() が t16 にバインドする
// 水面越しに見える背景色の取得に使用する
Texture2D<float4> gSceneColor : register(t16);

// ===== フレーム定数バッファ（VS と共有）=====
cbuffer WaterFrameConstants : register(b5)
{
    float4 gClipPlane;
    int gClipEnabled;
    int gReflectionEnabled; // 1 = 反射テクスチャ有効，0 = IBL フォールバック
    float gFresnelReflectanceScale; // Fresnel 反射率スケール
    float gFresnelBaseReflectance; // 正面入射時の反射率（F0）

    // ---- Depth Fade ----
    float gAbsorptionCoeff; // 光吸収係数（大きいほど短距離で不透明）
    int gDepthFadeEnabled; // 1 = Depth Fade 有効
    float2 gFramePad;

    // ---- 水色 ----
    float3 gShallowColor; // 浅瀬の水色
    float gShallowColorPad;
    float3 gDeepColor; // 深場の水色
    float gDeepColorPad;
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

    // 頂点シェーダーで再構築した Gerstner Wave 由来法線をそのまま使用する
    input.normal = normalize(input.normal);

    // 視線方向と alpha
    float3 toEye = normalize(gCamera.worldPosition - input.worldPosition);
    float finalAlpha = gMaterial.color.a;
    
    // アンリット
    if (gMaterial.enableLighting == 0)
    {
        output.color = gMaterial.color;
        return output;
    }

    // 水面はテクスチャを使わず、マテリアル値のみで PBR パラメータを構成する
    float metallic = gMaterial.metallic;
    float roughness = max(gMaterial.roughness, 0.01f);
    float ao = gMaterial.ao;

    float3 albedo = gMaterial.color.rgb;

    // PBR ライティング
    output.color.rgb = CalculateAllLighting(input, albedo, metallic, roughness, ao, toEye, float4(1.0f, 1.0f, 1.0f, 1.0f));
    output.color.a = finalAlpha;

    // IBL
    output.color.rgb += ApplyIBL(input, albedo, metallic, roughness, ao, toEye);

    return output;
}

PixelShaderOutput main(VertexShaderOutput input)
{
    // ---- 1. 水面専用 PBR フォワード出力をベースにする（discard なし）----
    PixelShaderOutput output = WaterForwardMain(input);
    float baseCoverage = saturate(output.color.a);

    // ---- 2. スクリーン UV を計算する ----
    float2 screenUV = input.clipPosCurrent.xy / input.clipPosCurrent.w;
    screenUV = screenUV * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);
    screenUV = saturate(screenUV);

    // ---- 3. Beer-Lambert による透過率と吸収量の計算 ----
    // GBuffer 深度テクスチャから背後のオブジェクトの深度を取得し、
    // 水面との深度差（水柱の厚さ）から透過率 transmittance を求める
    float transmittance = 1.0f;
    float absorption = 0.0f;
    float tintBlend = 0.0f;
    float3 waterTint = gShallowColor;
    if (gDepthFadeEnabled)
    {
        // near/far クリップ（カメラデフォルト値）
        const float kNear = 0.1f;
        const float kFar = 1000.0f;

        // シーン深度を NDC → ビュー空間線形深度（m）に変換する
        float sceneDepthNDC = gSceneDepth.Sample(gLinearClamp, screenUV).r;
        if (sceneDepthNDC < 0.99999f)
        {
            float sceneDepthView = LinearizeDepth(sceneDepthNDC, kNear, kFar);

            // 水面の NDC 深度 → ビュー空間線形深度（m）に変換する
            float waterDepthNDC = saturate(input.clipPosCurrent.z / input.clipPosCurrent.w);
            float waterDepthView = LinearizeDepth(waterDepthNDC, kNear, kFar);

            // 水柱の厚さ（m）= 背後オブジェクトの深度 - 水面の深度
            // 正値のみ使用（水面より前のオブジェクトは無視）
            float waterColumn = max(0.0f, sceneDepthView - waterDepthView);
            float extinction = waterColumn * max(gAbsorptionCoeff, 1.0e-4f);

            // Beer-Lambert 則: transmittance が透過した光量、absorption が吸収された量
            transmittance = exp(-extinction);
            absorption = 1.0f - transmittance;

            // 深さによる色遷移は消光量そのものではなく、より緩やかな深度係数で制御する。
            // 消光量をそのまま使うと浅瀬でもすぐ深場色へ寄り、暗さが飽和しやすい。
            tintBlend = 1.0f - exp(-extinction * 0.35f);
            waterTint = lerp(gShallowColor, gDeepColor, saturate(tintBlend));
        }
    }

    // ---- 4. 視線方向と Fresnel 係数を計算する ----
    float3 viewDir = normalize(gCamera.worldPosition - input.worldPosition);
    float3 geomNormal = normalize(input.normal);
    float cosTheta = saturate(dot(geomNormal, viewDir));
    float fresnel = FresnelSchlick(cosTheta, saturate(gFresnelBaseReflectance));
    float reflectanceWeight = saturate(fresnel * gFresnelReflectanceScale);

    // ---- 5. 標準 alpha ブレンドで背景光と整合するように、水面自身が追加する成分だけを組み立てる ----
    // 背景光は既にレンダーターゲット側に存在するため、ここで SceneColor を再度混ぜると二重減衰になる。
    // そのため、ピクセルシェーダーでは「背景から奪う割合」と「水面が足す成分」を分けて計算する。
    float transmissionWeight = transmittance * (1.0f - reflectanceWeight);

    // 吸収で失われた光をそのまま水色として再注入すると、散乱を持たない近似としては暗くなりすぎる。
    // そこで媒質寄与は一部だけを近似的に戻し、高さ差による透過変化を見えやすくする。
    float mediumWeight = absorption * 0.35f * (1.0f - reflectanceWeight);

    float3 reflectColor = output.color.rgb;
    if (gReflectionEnabled)
    {
        reflectColor = gReflectionTexture.Sample(gLinearClamp, screenUV).rgb;
    }

    float3 mediumContribution = waterTint * mediumWeight;
    float3 reflectionContribution = reflectColor * reflectanceWeight;
    float3 waterContribution = baseCoverage * (mediumContribution + reflectionContribution);

    output.color.a = saturate(baseCoverage * (1.0f - transmissionWeight));
    if (output.color.a > 1.0e-4f)
    {
        output.color.rgb = waterContribution / output.color.a;
    }
    else
    {
        output.color.rgb = 0.0f;
    }

    return output;
}
