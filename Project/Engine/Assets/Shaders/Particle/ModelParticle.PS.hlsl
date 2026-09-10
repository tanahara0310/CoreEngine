#include "Particle.hlsli"
#include "../Include/Lighting/LightStructures.hlsli"
#include "../Include/PBR/PBR.hlsli"
#include "Fog.hlsli"

// 板ポリパーティクル（Particle.PS.hlsl）と分けている理由:
// ビルボードは常にカメラを向くので法線に意味がなく、ライティングしても立体感が出ない。
// モデルパーティクルだけが形状由来の法線を持つので、こちらにライティングを入れる。

//SRVのregisterはt0
Texture2D<float32_t4> gTexture : register(t0);
StructuredBuffer<DirectionalLightData> gDirectionalLights : register(t1);
ConstantBuffer<LightCounts> gLightCounts : register(b0);
//Samplerのregisterはs0
SamplerState gSampler : register(s0);

// フォグ。C++ 側は「減衰のみ」バリアントを差す（板ポリ版と同じ理由）
ConstantBuffer<FogParameters> gFog : register(b1);

struct PixelShaderOutput
{
    float32_t4 color : SV_TARGET0;
};

/// @brief ディレクショナルライトのハーフランバート拡散を積算する
/// @param normal      正規化済みワールド法線
/// @param albedo      ライティング前のパーティクル色
/// @param outLitCount 寄与した（有効な）ライト数
/// @return 積算した拡散反射色
/// @details モデルパーティクルは metallic/roughness/法線マップを持たないので PBR ではなく
///          従来シェーディングを使う。ランバートだと裏面が真っ黒に潰れて粒の形が消えるため、
///          ハーフランバートで裏面にも最低限の明るさを残す。
/// @note    エネルギー保存の 1/π を掛けて PBR 経路（CalculatePBRLighting の拡散項）と
///          輝度スケールを揃える。これが無いと約 π 倍明るくなり、明部がトーンマッピングの
///          肩に張り付いて陰影の差が潰れる（＝ライティングが効いていないように見える）。
float32_t3 CalculateParticleDiffuse(float32_t3 normal, float32_t3 albedo, out uint32_t outLitCount)
{
    float32_t3 diffuse = float32_t3(0.0f, 0.0f, 0.0f);
    outLitCount = 0;

    for (uint32_t i = 0; i < gLightCounts.directionalLightCount; ++i)
    {
        if (gDirectionalLights[i].enabled == 0)
            continue;

        diffuse += CalculateHalfLambertDiffuse(
            normal,
            -normalize(gDirectionalLights[i].direction),
            gDirectionalLights[i].color.rgb,
            gDirectionalLights[i].intensity,
            albedo,
            1.0f) * INV_PI;
        ++outLitCount;
    }

    return diffuse;
}

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    //テクスチャをサンプリング
    float32_t4 textureColor = gTexture.Sample(gSampler, input.texcoord);

    // パーティクルの色とテクスチャ色を乗算
    float32_t4 finalColor = textureColor * input.color;

    if (finalColor.a <= 0.0f)
    {
        discard; // ピクセルを破棄
    }

    // 有効なディレクショナルライトが 1 つも無いシーンでは従来どおりアンリットで出す。
    // ライトが消えた瞬間にパーティクルが真っ黒になるのを防ぐフォールバック。
    uint32_t litCount = 0;
    float32_t3 diffuse = CalculateParticleDiffuse(normalize(input.normal), finalColor.rgb, litCount);
    if (litCount > 0)
    {
        finalColor.rgb = diffuse;
    }

    // アルファは通常の透明度としてそのまま出す。半透明（kBlendModeNormal）の
    // SrcBlend は SRC_ALPHA なので、ここで RGB へ事前乗算すると二重に掛かって黒ずむ。
    // 加算（kBlendModeAdd）も SrcBlend が SRC_ALPHA なので同じ扱いでよい。
    output.color.rgb = finalColor.rgb;
    output.color.a = finalColor.a;

    // フォグ（不透明・半透明と同じ数式。減衰のみ効く）
    output.color.rgb = ApplyFog(gFog, input.worldPosition, output.color.rgb);

    return output;
}
