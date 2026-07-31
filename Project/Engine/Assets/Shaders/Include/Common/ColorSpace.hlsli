/// @file ColorSpace.hlsli
/// @brief 輝度計算・トーンマップ・色空間変換の共通実装
/// @details 輝度の重み（Rec.709 / Rec.601）と ACES 近似カーブは各ポストエフェクトが
///          個別に持っていたため、係数がずれると同じ「明るさ」が箇所ごとに変わってしまう。
///          ここに集約して同一の定義を共有する。

#ifndef COLOR_SPACE_HLSLI
#define COLOR_SPACE_HLSLI

// ===================================================================
// 輝度
// ===================================================================
/// @brief Rec.709（sRGB 原色）の相対輝度の重み。リニア HDR 値に対して使う
static const float3 LUMA_WEIGHT_REC709 = float3(0.2126f, 0.7152f, 0.0722f);

/// @brief NTSC / Rec.601 の輝度の重み。セピアなど「見た目重視」の旧来効果向け
static const float3 LUMA_WEIGHT_REC601 = float3(0.299f, 0.587f, 0.114f);

/// @brief リニア RGB の相対輝度（Rec.709）
float Luminance(float3 color)
{
    return dot(color, LUMA_WEIGHT_REC709);
}

/// @brief Rec.601 重みの輝度（レトロ調エフェクト用。物理量としては使わないこと）
float LuminanceRec601(float3 color)
{
    return dot(color, LUMA_WEIGHT_REC601);
}

// ===================================================================
// トーンマッピング
// ===================================================================
/// @brief ACES フィルミックトーンマップの近似（Krzysztof Narkowicz 版）
/// @param x リニア HDR 値（露出補正適用済み）
/// @return [0,1] に収めた LDR 値
float3 ACESFilm(float3 x)
{
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// ===================================================================
// ガンマ / sRGB
// ===================================================================
/// @brief リニア → sRGB（IEC 61966-2-1 の正確な区分関数）
/// @note 成分ごとの分岐は step + lerp で書く。HLSL 2021 ではベクタ条件の三項演算子が
///       エラーになる（select が必要）ため、バージョン非依存なこちらを使う。
float3 LinearToSRGB(float3 linearColor)
{
    float3 lo = linearColor * 12.92f;
    float3 hi = 1.055f * pow(max(linearColor, 0.0f), 1.0f / 2.4f) - 0.055f;
    return lerp(lo, hi, step(0.0031308f, linearColor));
}

/// @brief sRGB → リニア（IEC 61966-2-1 の正確な区分関数）
float3 SRGBToLinear(float3 srgbColor)
{
    float3 lo = srgbColor / 12.92f;
    float3 hi = pow(max(srgbColor + 0.055f, 0.0f) / 1.055f, 2.4f);
    return lerp(lo, hi, step(0.04045f, srgbColor));
}

#endif // COLOR_SPACE_HLSLI
