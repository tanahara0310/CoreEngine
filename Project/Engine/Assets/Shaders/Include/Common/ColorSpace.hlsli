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

/// @brief GT トーンマップ（Uchimura 2017・グランツーリスモ方式）
/// @details リニア区間を明示的に持つのが特徴で、中間調の色が ACES より素直に残る。
///          チャンネル別に適用するため、ハイライトは自然に彩度が抜けて白へ収束する。
///          定数は論文の推奨値（最大輝度 P=1.0 / コントラスト a=1.0 / リニア区間開始 m=0.22 /
///          リニア区間長 l=0.4 / 黒の締まり c=1.33 / 黒オフセット b=0.0）
float GTToneCurve(float x)
{
    const float P = 1.0f;  // 最大輝度
    const float a = 1.0f;  // コントラスト（リニア区間の傾き）
    const float m = 0.22f; // リニア区間の開始
    const float l = 0.4f;  // リニア区間の長さ
    const float c = 1.33f; // 黒の締まり
    const float b = 0.0f;  // 黒オフセット

    const float l0 = ((P - m) * l) / a;
    const float S0 = m + l0;
    const float S1 = m + a * l0;
    const float C2 = (a * P) / (P - S1);
    const float CP = -C2 / P;

    const float w0 = 1.0f - smoothstep(0.0f, m, x);
    const float w2 = step(m + l0, x);
    const float w1 = 1.0f - w0 - w2;

    const float T = m * pow(max(x / m, 0.0f), c) + b;       // 暗部（トゥ）
    const float L = m + a * (x - m);                          // リニア区間
    const float S = P - (P - S1) * exp(CP * (x - S0));        // 肩

    return T * w0 + L * w1 + S * w2;
}

/// @brief GT トーンマップ（3 チャンネル）
float3 GTTonemap(float3 x)
{
    return float3(GTToneCurve(x.r), GTToneCurve(x.g), GTToneCurve(x.b));
}

/// @brief AgX のシグモイドの 6 次多項式近似（Benjamin Wrensch 版）
float3 AgXContrastApprox(float3 x)
{
    const float3 x2 = x * x;
    const float3 x4 = x2 * x2;
    return +15.5f * x4 * x2
           - 40.14f * x4 * x
           + 31.96f * x4
           - 6.868f * x2 * x
           + 0.4298f * x2
           + 0.1191f * x
           - 0.00232f;
}

/// @brief AgX トーンマップ（Troy Sobotka / Blender 4.x 既定）
/// @details 高彩度・高輝度の光源を色相を回さずに白へ抜くのが最大の強み。
///          彩度の高い夕日や炎が「濃い色のまま張り付く」のを構造的に防ぐ。
/// @param val リニア HDR 値（露出補正適用済み）
/// @return リニア LDR 値（内部の表示エンコードは 2.2 ガンマ相当なので、
///         パイプラインの前提であるリニアへ戻してから返す）
float3 AgXTonemap(float3 val)
{
    // インセット行列（原色を内側へ寄せ、色域の角の破綻を防ぐ）
    const float3x3 kAgxMat = float3x3(
        0.842479062253094f,  0.0784335999999992f, 0.0792237451477643f,
        0.0423282422610123f, 0.878468636469772f,  0.0791661274605434f,
        0.0423756549057051f, 0.0784336f,          0.879142973793104f);
    const float3x3 kAgxMatInv = float3x3(
        1.19687900512017f,   -0.0980208811401368f, -0.0990297440797205f,
        -0.0528968517574562f, 1.15190312990417f,   -0.0989611768448433f,
        -0.0529716355144438f, -0.0980434501171241f, 1.15107367264116f);
    const float kMinEv = -12.47393f;
    const float kMaxEv = 4.026069f;

    val = mul(kAgxMat, val);
    val = clamp(log2(max(val, 1e-10f)), kMinEv, kMaxEv);
    val = (val - kMinEv) / (kMaxEv - kMinEv);
    val = AgXContrastApprox(val);
    val = mul(kAgxMatInv, val);

    // ここまでは 2.2 ガンマ相当の表示エンコード値。リニアへ戻す
    return saturate(pow(max(val, 0.0f), 2.2f));
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
