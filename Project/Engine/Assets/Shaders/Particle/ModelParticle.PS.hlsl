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

/// @brief 4x4 Bayer ディザのしきい値（0..1）を返す
/// @param pixelPosition SV_POSITION の xy（ピクセル座標）
/// @details モデルパーティクルは不透明（kBlendModeNone）で描く前提なので、アルファを
///          下げても透けずに黒くなるだけ。半透明ブレンドへ切り替えると今度は深度書き込みが
///          切られ、後から来る SkyBox / VolumetricCloud に塗り潰されて粒ごと消える。
///          そこでアルファは「ピクセルの残す割合」として使い、不透明のまま間引いて消す。
/// @note しきい値は 1/32 〜 31/32。アルファ 0 で全ピクセルが消え、1 で全て残る。
float32_t DitherThreshold(float32_t2 pixelPosition)
{
    static const float32_t kBayer4x4[16] =
    {
         0.5f / 16.0f,  8.5f / 16.0f,  2.5f / 16.0f, 10.5f / 16.0f,
        12.5f / 16.0f,  4.5f / 16.0f, 14.5f / 16.0f,  6.5f / 16.0f,
         3.5f / 16.0f, 11.5f / 16.0f,  1.5f / 16.0f,  9.5f / 16.0f,
        15.5f / 16.0f,  7.5f / 16.0f, 13.5f / 16.0f,  5.5f / 16.0f
    };

    const uint32_t2 cell = uint32_t2(pixelPosition) & 3;
    return kBayer4x4[cell.y * 4 + cell.x];
}

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    //テクスチャをサンプリング
    float32_t4 textureColor = gTexture.Sample(gSampler, input.texcoord);

    // パーティクルの色とテクスチャ色を乗算
    float32_t4 finalColor = textureColor * input.color;

    // アルファはピクセルの間引き率として使う（DitherThreshold のコメント参照）。
    // 不透明のまま、粒を少しずつ溶かして消せる。
    if (finalColor.a < DitherThreshold(input.position.xy))
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

    // アルファはディザで消費済みなので、残ったピクセルは明るさを落とさずそのまま出す
    // （ここで乗算すると、消えかけの粒がディザで薄くなりつつ黒ずむ）。
    output.color.rgb = finalColor.rgb;
    output.color.a = 1.0f;

    // フォグ（不透明・半透明と同じ数式。減衰のみ効く）
    output.color.rgb = ApplyFog(gFog, input.worldPosition, output.color.rgb);

    return output;
}
