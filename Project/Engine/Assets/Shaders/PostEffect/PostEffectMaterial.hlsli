// ============================================================
// プロジェクトのポストエフェクト（Project.json の postEffects）のシェーダが読み込む共通の定義
// コンピュートシェーダの入口は main。スレッドグループは 8×8（POST_EFFECT_THREADS）
// ============================================================
#ifndef POST_EFFECT_MATERIAL_HLSLI
#define POST_EFFECT_MATERIAL_HLSLI

#include "ColorSpace.hlsli"

/// 前の段までの画面
Texture2D<float4> gTexture : register(t0);

/// 書き出し先（全部の画素に書く）
RWTexture2D<float4> gOutput : register(u0);

/// 描くたびにエンジンが渡す値
struct PostEffectFrameData
{
    uint2  screenSize;    ///< 画面の大きさ（px）
    float2 invScreenSize; ///< 1 ÷ 画面の大きさ
    float  time;          ///< 起動からの時間（時間の倍率が掛かる。スプライトのマテリアルと同じ時計）
    float  unscaledTime;  ///< 起動からの実時間
    float  deltaTime;     ///< 前のフレームからの時間（時間の倍率が掛かる）
    float  padding;
};
ConstantBuffer<PostEffectFrameData> gPostEffect : register(b1);

SamplerState gSampler : register(s0);      ///< 線形・端で止める
SamplerState gPointSampler : register(s1); ///< 最も近い画素・端で止める
SamplerState gWrapSampler : register(s2);  ///< 線形・繰り返し

#define POST_EFFECT_THREADS [numthreads(8, 8, 1)]

/// @brief 画面の外の画素か（スレッドグループのはみ出し）
bool IsOutsideScreen(uint2 pixel)
{
    return pixel.x >= gPostEffect.screenSize.x || pixel.y >= gPostEffect.screenSize.y;
}

/// @brief 画素の中心の UV（左上が (0, 0)）
float2 PostEffectUV(uint2 pixel)
{
    return (float2(pixel) + 0.5f) * gPostEffect.invScreenSize;
}

/// @brief 画素の色をそのまま読む
float4 LoadScreen(int2 pixel)
{
    int2 clamped = clamp(pixel, int2(0, 0), int2(gPostEffect.screenSize) - 1);
    return gTexture.Load(int3(clamped, 0));
}

/// @brief UV で画面を読む（線形・端で止める）
float4 SampleScreen(float2 uv)
{
    return gTexture.SampleLevel(gSampler, uv, 0.0f);
}

#endif // POST_EFFECT_MATERIAL_HLSLI
