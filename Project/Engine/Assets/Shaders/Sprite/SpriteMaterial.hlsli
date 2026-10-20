// ============================================================
// マテリアル（.mat）のシェーダが読み込む共通の定義
// スプライト描画と UI 画像の両方で使う
// ============================================================
#ifndef SPRITE_MATERIAL_HLSLI
#define SPRITE_MATERIAL_HLSLI

#include "ColorSpace.hlsli"

/// 頂点シェーダへの入力（板の 4 頂点）
struct SpriteVertexInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal   : NORMAL0;
};

/// 頂点シェーダからピクセルシェーダへ渡す値
struct SpriteVertexOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0; ///< コマ割り・反転を済ませた UV
    float3 worldPos : TEXCOORD1; ///< スプライトはワールド座標、UI 画像は画面の px（左上が原点・Y は下向き）
};

/// 板の変換
struct SpriteTransform
{
    float4x4 WVP;
    float4x4 World;
};

/// 描くたびにエンジンが渡す値
struct SpriteDrawData
{
    float4   color;        ///< 部品の色（sRGB）
    float4x4 uvTransform;  ///< 部品の UV の移動・倍率・回転
    float2   textureSize;  ///< テクスチャの大きさ（px）
    float    time;         ///< 起動からの時間（時間の倍率が掛かる）
    float    unscaledTime; ///< 起動からの実時間
    float    deltaTime;    ///< 前のフレームからの時間（時間の倍率が掛かる）
    uint     filterMode;   ///< 0 = 線形 / 1 = ポイント
    float2   padding;
};

ConstantBuffer<SpriteTransform> gSpriteTransform : register(b1);
ConstantBuffer<SpriteDrawData> gSpriteDraw : register(b2);

/// 部品のテクスチャ（テクスチャを指していない UI 画像は白）
Texture2D<float4> gTexture : register(t0);

SamplerState gSampler : register(s0);      ///< 線形・繰り返し
SamplerState gPointSampler : register(s1); ///< 最も近い画素・繰り返し
SamplerState gClampSampler : register(s2); ///< 線形・端で止める

/// @brief 部品の UV 変換を掛けた UV
float2 SpriteUV(float2 texcoord)
{
    return mul(float4(texcoord, 0.0f, 1.0f), gSpriteDraw.uvTransform).xy;
}

/// @brief 部品のフィルタの設定で gTexture を読む
float4 SampleSprite(float2 uv)
{
    return (gSpriteDraw.filterMode == 1)
        ? gTexture.Sample(gPointSampler, uv)
        : gTexture.Sample(gSampler, uv);
}

/// @brief 部品の色（リニア）
float4 SpriteColor()
{
    return float4(SRGBToLinear(gSpriteDraw.color.rgb), gSpriteDraw.color.a);
}

/// @brief マテリアルを付けていないときと同じ色（テクスチャ × 部品の色）
float4 SpriteDefaultColor(SpriteVertexOutput input)
{
    return SpriteColor() * SampleSprite(SpriteUV(input.texcoord));
}

#endif // SPRITE_MATERIAL_HLSLI
