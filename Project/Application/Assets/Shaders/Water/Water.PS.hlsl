#include "../../../../Engine/Assets/Shaders/Include/Object/Object3dForward.hlsli"

// ===== 反射テクスチャ（Planar Reflection RTT）=====
Texture2D<float4> gReflectionTexture : register(t14);
SamplerState      gLinearClamp       : register(s2);

// ===== シーン深度テクスチャ（GBuffer 深度 / Depth Stencil SRV）=====
// WaterPlaneObject::BindCustomResources() が t15 にバインドする
// Depth Fade（Beer-Lambert）で水柱の厚さを計算するために使用する
Texture2D<float>  gSceneDepth        : register(t15);

// ===== シーンカラーテクスチャ（OffScreen Color SRV）=====
// WaterPlaneObject::BindCustomResources() が t16 にバインドする
// 水面越しに見える背景色の取得に使用する
Texture2D<float4> gSceneColor        : register(t16);

// ===== フレーム定数バッファ（VS と共有）=====
cbuffer WaterFrameConstants : register(b5)
{
    float4 gClipPlane;
    int    gClipEnabled;
    int    gReflectionEnabled;  // 1 = 反射テクスチャ有効，0 = IBL フォールバック
    float  gFresnelReflectanceScale; // Fresnel 反射率スケール
    float  gFresnelBaseReflectance;  // 正面入射時の反射率（F0）

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

    // 頂点シェーダーで再構築した Gerstner Wave 由来法線をそのまま使用する
    input.normal = normalize(input.normal);

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

    // ---- 3. Beer-Lambert による透過率と吸収量の計算 ----
    // GBuffer 深度テクスチャから背後のオブジェクトの深度を取得し、
    // 水面との深度差（水柱の厚さ）から透過率 transmittance を求める
    float transmittance = 1.0f;
    float absorption = 0.0f;
    float3 waterTint = gShallowColor;
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

        // Beer-Lambert 則: transmittance が透過した光量、absorption が吸収された量
        transmittance = exp(-waterColumn * gAbsorptionCoeff);
        absorption = 1.0f - transmittance;

        // 吸収量に応じて浅瀬色 → 深場色へ移行する
        waterTint = lerp(gShallowColor, gDeepColor, saturate(absorption));
    }

    // 水中を通ってきた背景光は透過率で減衰させ、吸収された分だけ水の色を乗せる
    float3 sceneColor = gSceneColor.Sample(gLinearClamp, screenUV).rgb;
    float3 transmittedColor = sceneColor * transmittance;
    output.color.rgb = transmittedColor + waterTint * absorption;

    // ---- 4. 視線方向と Fresnel 係数を計算する ----
    float3 viewDir    = normalize(gCamera.worldPosition - input.worldPosition);
    float3 geomNormal = normalize(input.normal);
    float  cosTheta   = saturate(dot(geomNormal, viewDir));
    float  fresnel    = FresnelSchlick(cosTheta, saturate(gFresnelBaseReflectance));
    float  reflectanceWeight = saturate(fresnel * gFresnelReflectanceScale);

    // ---- 5. alpha は水面材質のベース値を維持しつつ、吸収量と Fresnel で不透明さを調整する ----
    // 以前の max(baseAlpha, absorption) は baseAlpha=1 のとき常に 1 になりやすく、
    // どの角度から見ても水中が見えない原因になっていた。
    // ここではベースアルファを上限にし、浅瀬・正面視ではより透け、
    // 深場・斜め視では不透明寄りになるようにする。
    float opacityFromMedium = saturate(max(absorption, reflectanceWeight));
    float minimumSurfaceAlpha = output.color.a * 0.35f;
    output.color.a = saturate(lerp(minimumSurfaceAlpha, output.color.a, opacityFromMedium));

    // ---- 6. 反射色をブレンドする ----
    if (gReflectionEnabled)
    {
        float3 reflectColor = gReflectionTexture.Sample(gLinearClamp, screenUV).rgb;
        output.color.rgb = lerp(output.color.rgb, reflectColor, reflectanceWeight);
    }

    return output;
}
