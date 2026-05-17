#include "../../../../Engine/Assets/Shaders/Include/Object/Object3dForward.hlsli"

// ===== 反射テクスチャ（Planar Reflection RTT）=====
// WaterPlaneObject::BindCustomResources() が t14 にバインドする
// gReflectionEnabled == 0 のときはサンプリングしない
Texture2D<float4> gReflectionTexture : register(t14);
SamplerState      gLinearClamp       : register(s2);

// ===== フレーム定数バッファ（VS と共有）=====
// Water.VS.hlsl の WaterFrameConstants と同一レイアウトにすること
cbuffer WaterFrameConstants : register(b5)
{
    float4 gClipPlane;
    int    gClipEnabled;
    int    gReflectionEnabled; // 1 = 反射テクスチャ有効，0 = IBL フォールバック
    float2 gFramePadding;
};

/// @brief Schlick 近似による Fresnel 係数を計算する
/// @param cosTheta  視線と法線のなす角の余弦（saturate 済み推奨）
/// @param f0        法線入射時の反射率（水面 ≈ 0.02）
float FresnelSchlick(float cosTheta, float f0)
{
    return f0 + (1.0f - f0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

PixelShaderOutput main(VertexShaderOutput input)
{
    // ---- 1. 通常の PBR フォワード出力をベースにする ----
    PixelShaderOutput output = ForwardMain(input);

    // ---- 2. 視線方向と Fresnel 係数を計算する ----
    float3 viewDir  = normalize(gCamera.worldPosition - input.worldPosition);
    // ジオメトリ法線（上向き成分）を使うことで法線マップの細かい凹凸に引きずられない
    float3 geomNormal = normalize(float3(0.0f, 1.0f, 0.0f));
    float  cosTheta   = saturate(dot(geomNormal, viewDir));
    float  fresnel    = FresnelSchlick(cosTheta, 0.02f);

    // ---- 3. 反射色を取得する ----
    // ---- 3. 反射色をブレンドする ----
    if (gReflectionEnabled)
    {
        // Planar Reflection RTT をスクリーン UV でサンプリングする
        // NDC → UV: x: [-1,1]→[0,1]  y: [1,-1]→[0,1]
        float2 screenUV = input.clipPosCurrent.xy / input.clipPosCurrent.w;
        screenUV = screenUV * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);
        // UV を [0,1] にクランプしてボーダーアーティファクトを防ぐ
        screenUV = saturate(screenUV);
        float3 reflectColor = gReflectionTexture.Sample(gLinearClamp, screenUV).rgb;

        // ---- 4. Fresnel ブレンド（反射 RTT がある場合のみ）----
        // ForwardMain() が既に ApplyIBL() を含むため IBL は二重加算しない
        // Planar Reflection を優先してブレンドし、IBL の上書きとする
        output.color.rgb = lerp(output.color.rgb, reflectColor, fresnel);
    }
    // gReflectionEnabled == 0 の場合は ForwardMain() の IBL 結果（ApplyIBL）をそのまま使用する
    // ここで gPrefilteredMap を再サンプリングすると IBL が二重加算されてしまうため行わない

    return output;
}
