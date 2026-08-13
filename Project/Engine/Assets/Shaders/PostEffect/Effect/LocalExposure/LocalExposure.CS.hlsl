// LocalExposure.CS.hlsl - ローカル露出の適用パス
//
// グローバル露出は「空に合わせると影が潰れ、影に合わせると空が飛ぶ」の二択しかない。
// ここでは対数輝度を「ベース層（ぼかした低周波）」と「ディテール層（残差）」に分け、
// ベース層だけを中間グレーのアンカーへ向かって圧縮する。
// ディテール（模様・質感のコントラスト）はそのまま残るので、
// 全体を持ち上げても HDR 写真のような眠さが出にくい。
//
// ベース層は 1/8 解像度のガウシアンぼかしなので、そのまま使うと強いエッジの周りに
// ハロ（明暗の縁取り）が出る。適用時にフル解像度の対数輝度をガイドにした
// ジョイントバイラテラルアップサンプルで再構成し、エッジを復元してハロを抑える。

#include "ColorSpace.hlsli" // Luminance

Texture2D<float4> gTexture : register(t0);   // シーンカラー（リニアHDR）
Texture2D<float> gBlurredLogLum : register(t1); // ぼかし済み対数輝度（1/8解像度）
RWTexture2D<float4> gOutput : register(u0);

cbuffer LocalExposureParams : register(b0)
{
    uint2 screenSize;
    uint2 baseSize;          // 1/8 解像度の実寸法
    float highlightContrast; // アンカーより明るいベースの圧縮率（1で無効）
    float shadowContrast;    // アンカーより暗いベースの圧縮率（1で無効）
    float detailStrength;    // ディテール層の倍率（通常 1）
    float middleGreyBias;    // アンカーの EV オフセット（0 = リニア0.18）
    float rangeSigma;        // バイラテラルの輝度許容幅 [EV]
    float3 pad;
};

static const uint kGroupSize = 8;

/// @brief ガイド付きバイラテラルアップサンプル
/// @details 1/8 解像度のベース層を、フル解像度の対数輝度 guideLog をガイドに補間する。
///          輝度が大きく違う（=エッジの向こう側の）低解像度テクセルは重みが落ちるので、
///          ガウシアンがエッジを越えて運んできた値が復元時に棄却される
float SampleBaseBilateral(float2 uv, float guideLog)
{
    const float2 baseCoord = uv * float2(baseSize) - 0.5f;
    const int2 base0 = int2(floor(baseCoord));
    const float2 f = baseCoord - float2(base0);

    const float bilinearW[4] = {
        (1.0f - f.x) * (1.0f - f.y),
        f.x * (1.0f - f.y),
        (1.0f - f.x) * f.y,
        f.x * f.y
    };
    const int2 offsets[4] = { int2(0, 0), int2(1, 0), int2(0, 1), int2(1, 1) };

    const float invRange = 1.0f / max(2.0f * rangeSigma * rangeSigma, 1e-4f);

    float sum = 0.0f;
    float weightSum = 0.0f;
    float fallback = 0.0f; // バイラテラル重みが全滅したときのための通常バイリニア
    for (int i = 0; i < 4; ++i)
    {
        const int2 p = clamp(base0 + offsets[i], int2(0, 0), int2(baseSize) - 1);
        const float value = gBlurredLogLum.Load(int3(p, 0));
        const float diff = guideLog - value;
        const float w = bilinearW[i] * exp(-diff * diff * invRange);
        sum += value * w;
        weightSum += w;
        fallback += value * bilinearW[i];
    }

    return (weightSum > 1e-5f) ? (sum / weightSum) : fallback;
}

[numthreads(kGroupSize, kGroupSize, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    if (dispatchId.x >= screenSize.x || dispatchId.y >= screenSize.y)
    {
        return;
    }

    const int2 coord = int2(dispatchId.xy);
    const float4 color = gTexture.Load(int3(coord, 0));

    const float lum = Luminance(color.rgb);
    const float logLum = log2(max(lum, 1e-6f));

    const float2 uv = (float2(coord) + 0.5f) / float2(screenSize);
    const float base = SampleBaseBilateral(uv, logLum);
    const float detail = logLum - base;

    // ベース層だけをアンカー（リニア中間グレー 0.18 + バイアス）へ向けて圧縮する
    const float anchor = log2(0.18f) + middleGreyBias;
    const float contrast = (base > anchor) ? highlightContrast : shadowContrast;
    const float newBase = anchor + (base - anchor) * contrast;

    const float newLogLum = newBase + detail * detailStrength;

    // 色は保ったまま輝度だけを乗算で動かす
    const float multiplier = exp2(newLogLum - logLum);
    gOutput[coord] = float4(color.rgb * multiplier, color.a);
}
