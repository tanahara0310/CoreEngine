#include "Object3dForward.hlsli"

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

struct WaterPSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float3 worldPosition : POSITION0;
    float4 lightSpacePos : POSITION1;
    float3 tangent : TANGENT0;
    float3 bitangent : BINORMAL0;
    float4 clipPosCurrent : POSITION2;
    float4 clipPosPrev : POSITION3;
};

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
    int gDepthFadeDebugEnabled; // 1 = 水深デバッグ表示
    float gDepthFadeDebugScale; // 水深デバッグ表示倍率

    // ---- 水色 ----
    float3 gShallowColor; // 浅瀬の水色
    float gShallowColorPad;
    float3 gDeepColor; // 深場の水色
    float gDeepColorPad;

    // ---- デバッグ表示 ----
    uint gDepthDebugViewMode;
    uint gLightningImpactCount;
    float2 gDebugPadding;

    struct WaterLightningImpactData
    {
        float2 center;
        float radius;
        float intensity;
        float chargeRadius;
        float chargeIntensity;
        float impactTime;
        float screenFlash;
    };

    WaterLightningImpactData gLightningImpacts[6];
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

/// @brief ビュー空間Z差をスクリーンレイ上の実際の光路長へ変換する
/// @param viewDepthDelta 背景と水面のビュー空間Z差
/// @param waterDepthView 水面のビュー空間Z深度
/// @param worldPos 水面ピクセルのワールド座標
/// @return Beer-Lambert に使う水中光路長
float ComputeWaterOpticalPathLength(float viewDepthDelta, float waterDepthView, float3 worldPos)
{
    float rayDistanceToWater = length(gCamera.worldPosition - worldPos);
    float viewToRayScale = rayDistanceToWater / max(waterDepthView, 1.0e-4f);
    return max(0.0f, viewDepthDelta * viewToRayScale);
}

/// @brief Schlick 近似による Fresnel 係数を計算する
/// @param cosTheta  視線と法線のなす角の余弦（saturate 済み推奨）
/// @param f0        法線入射時の反射率（水面 ≈ 0.02）
float FresnelSchlick(float cosTheta, float f0)
{
    return f0 + (1.0f - f0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

float3 VisualizeDepthValue(float value)
{
    return float3(value, value, value);
}

/// @brief 水面専用フォワードパス処理（ForwardMain の discard 処理を削除したバージョン）
/// 水面は常に描画され、alpha 値で透明度を制御する
VertexShaderOutput ToVertexShaderOutput(WaterPSInput input)
{
    VertexShaderOutput output;
    output.position = input.position;
    output.texcoord = input.texcoord;
    output.normal = input.normal;
    output.worldPosition = input.worldPosition;
    output.lightSpacePos = input.lightSpacePos;
    output.tangent = input.tangent;
    output.bitangent = input.bitangent;
    output.clipPosCurrent = input.clipPosCurrent;
    output.clipPosPrev = input.clipPosPrev;
    return output;
}

PixelShaderOutput WaterForwardMain(WaterPSInput input)
{
    VertexShaderOutput forwardInput = ToVertexShaderOutput(input);
    PixelShaderOutput output;

    // 頂点シェーダーで再構築した Gerstner Wave 由来法線をそのまま使用する
    forwardInput.normal = normalize(forwardInput.normal);

    // 視線方向と alpha
    float3 toEye = normalize(gCamera.worldPosition - forwardInput.worldPosition);
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
    output.color.rgb = CalculateAllLighting(forwardInput, albedo, metallic, roughness, ao, toEye, float4(1.0f, 1.0f, 1.0f, 1.0f));
    output.color.a = finalAlpha;

    // IBL
    output.color.rgb += ApplyIBL(forwardInput, albedo, metallic, roughness, ao, toEye);

    return output;
}

PixelShaderOutput main(WaterPSInput input)
{
    // ---- 1. 水面専用 PBR フォワード出力をベースにする（discard なし）----
    PixelShaderOutput output = WaterForwardMain(input);
    float baseCoverage = saturate(output.color.a);

    // ---- 2. スクリーン UV を計算する ----
    uint sceneDepthWidth = 1;
    uint sceneDepthHeight = 1;
    gSceneDepth.GetDimensions(sceneDepthWidth, sceneDepthHeight);
    float2 screenUV = input.position.xy / float2(sceneDepthWidth, sceneDepthHeight);
    screenUV = saturate(screenUV);

    // ---- 3. Beer-Lambert による透過率と吸収量の計算 ----
    // 必要なのは「水中を通った光路長」なので、視線上の線形深度差を使う
    float transmittance = 1.0f;
    float absorption = 0.0f;
    float tintBlend = 0.0f;
    float3 waterTint = gShallowColor;
    float sceneDepthNDC = 1.0f;
    float waterDepthNDC = saturate(input.position.z);
    float sceneDepthView = 0.0f;
    float waterDepthView = 0.0f;
    float waterColumn = 0.0f;
    bool hasValidDepthFade = false;
    if (gDepthFadeEnabled)
    {
        // near/far クリップ（カメラデフォルト値）
        const float kNear = 0.1f;
        const float kFar = 1000.0f;

        // シーン深度を NDC → ビュー空間線形深度（m）に変換する
        sceneDepthNDC = gSceneDepth.Sample(gLinearClamp, screenUV).r;
        waterDepthView = LinearizeDepth(waterDepthNDC, kNear, kFar);

        if (sceneDepthNDC < 0.99999f)
        {
            sceneDepthView = LinearizeDepth(sceneDepthNDC, kNear, kFar);

            if (sceneDepthView > waterDepthView + 1.0e-4f)
            {
                hasValidDepthFade = true;

                // 水面自身の線形深度を求め、背景との深度差を水中の光路長として扱う
                waterColumn = ComputeWaterOpticalPathLength(
                    sceneDepthView - waterDepthView,
                    waterDepthView,
                    input.worldPosition);
                float extinction = waterColumn * max(gAbsorptionCoeff, 1.0e-4f);

                // Beer-Lambert 則: transmittance が透過した光量、absorption が吸収された量
                transmittance = exp(-extinction);
                absorption = 1.0f - transmittance;

                // Beer-Lambert の吸収量そのものを色遷移に使う
                // 浅い場所では吸収が少なく、深い場所では吸収が増えるため
                // そのまま shallow/deep の補間係数として扱う
                tintBlend = absorption;
                waterTint = lerp(gShallowColor, gDeepColor, saturate(tintBlend));
            }
        }

        if (!hasValidDepthFade)
        {
            // 深度が取得できない、または水面より奥の背景深度が存在しない場合でも
            // 水面の被覆率が極端に失われないよう、安定した浅瀬寄りの見た目へフォールバックする。
            absorption = saturate(baseCoverage * 0.65f);
            transmittance = 1.0f - absorption;
            tintBlend = absorption;
            waterTint = lerp(gShallowColor, gDeepColor, saturate(tintBlend));
        }
    }

    if (gDepthFadeDebugEnabled != 0)
    {
        output.color.a = 1.0f;

        if (gDepthDebugViewMode == 1)
        {
            float rawDelta = saturate((sceneDepthNDC - waterDepthNDC) * max(gDepthFadeDebugScale, 1.0f));
            output.color.rgb = float3(sceneDepthNDC, waterDepthNDC, rawDelta);
            if (sceneDepthNDC <= waterDepthNDC + 1.0e-5f)
            {
                output.color.rgb = float3(1.0f, 0.0f, 0.0f);
            }
            return output;
        }

        if (gDepthDebugViewMode == 2)
        {
            float sceneLinear = saturate(sceneDepthView / max(gDepthFadeDebugScale, 1.0e-4f));
            float waterLinear = saturate(waterDepthView / max(gDepthFadeDebugScale, 1.0e-4f));
            output.color.rgb = float3(sceneLinear, waterLinear, saturate((sceneDepthView - waterDepthView) / max(gDepthFadeDebugScale, 1.0e-4f)));
            return output;
        }

        if (gDepthDebugViewMode == 3)
        {
            float debugValue = 1.0f - exp(-waterColumn * max(gDepthFadeDebugScale, 1.0e-4f));
            output.color.rgb = VisualizeDepthValue(debugValue);
            return output;
        }

        if (gDepthDebugViewMode == 4)
        {
            output.color.rgb = float3(screenUV, 0.0f);
            return output;
        }

        if (gDepthDebugViewMode == 5)
        {
            output.color.rgb = gSceneColor.Sample(gLinearClamp, screenUV).rgb;
            return output;
        }

        if (gDepthDebugViewMode == 6)
        {
            output.color.rgb = gReflectionEnabled ? gReflectionTexture.Sample(gLinearClamp, screenUV).rgb : float3(1.0f, 0.0f, 1.0f);
            return output;
        }

        if (gDepthDebugViewMode == 7)
        {
            float3 debugViewDir = normalize(gCamera.worldPosition - input.worldPosition);
            float3 debugGeomNormal = normalize(input.normal);
            float debugCosTheta = saturate(dot(debugGeomNormal, debugViewDir));
            float debugFresnel = FresnelSchlick(debugCosTheta, saturate(gFresnelBaseReflectance));
            output.color.rgb = VisualizeDepthValue(debugFresnel);
            return output;
        }

        output.color.rgb = float3(1.0f, 1.0f, 0.0f);
        return output;
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

    // 吸収量をそのまま媒質寄与へ反映し、深いほど暗く色付きが強くなるようにする
    float mediumWeight = absorption * (1.0f - reflectanceWeight);

    float3 reflectColor = output.color.rgb;
    if (gReflectionEnabled)
    {
        float3 planarReflection = gReflectionTexture.Sample(gLinearClamp, screenUV).rgb;
        reflectColor += planarReflection;
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
