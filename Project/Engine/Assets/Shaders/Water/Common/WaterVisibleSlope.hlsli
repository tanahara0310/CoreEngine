// ============================================================
// 見えている面の傾きの統計（Water.PS と RTWaterReflection で共有）
// ------------------------------------------------------------
// 法線に入っていない細かい傾きを、軸ごとに分散 σ² の正規分布とみなす。
// 視線から見える面は視線の方を向いた面ほど広く見えるので、
// 重み (cosθ + p sinθ)+ を掛けた分布で平均とばらつきを求める。
// p は視線を含む縦の面での傾き（カメラから遠ざかる向きに上がると正）。
// ============================================================
#ifndef WATER_VISIBLE_SLOPE_INCLUDED
#define WATER_VISIBLE_SLOPE_INCLUDED

/// @brief 見えている面の、視線を含む縦の面での傾きの平均とばらつき
struct VisibleSlopeMoments
{
    float mean; ///< 傾きの平均（正 = 面がカメラの方を向く）
    float std;  ///< 傾きの標準偏差
};

/// @brief 誤差関数の近似（Abramowitz-Stegun 7.1.26）
float WaterErfApprox(float x)
{
    const float s = sign(x);
    const float a = abs(x);
    const float t = 1.0f / (1.0f + 0.3275911f * a);
    const float poly = ((((1.061405429f * t - 1.453152027f) * t + 1.421413741f) * t - 0.284496736f) * t
        + 0.254829592f) * t;
    return s * (1.0f - poly * exp(-a * a));
}

/// @brief 見えている面の傾きの平均とばらつきを返す
/// @param cosTheta      視線と面の法線のなす角の余弦
/// @param slopeVariance 軸ごとの傾きの分散 σ²（平均二乗傾斜の半分）
VisibleSlopeMoments ComputeVisibleSlopeMoments(float cosTheta, float slopeVariance)
{
    const float c = saturate(cosTheta);
    const float s = max(sqrt(1.0f - c * c), 1.0e-4f);
    const float sigma2 = max(slopeVariance, 1.0e-8f);
    const float sigma = sqrt(sigma2);
    const float mu = c / s;
    const float a = mu / sigma;
    // 標準正規分布の累積分布 Φ(a) と密度 φ(a)
    const float cdf = 0.5f * (1.0f + WaterErfApprox(a * 0.70710678f));
    const float pdf = 0.39894228f * exp(-0.5f * a * a);
    // 重みの 0・1・2 次のモーメント
    const float m0 = mu * cdf + sigma * pdf;
    const float m1 = sigma2 * cdf;
    const float m2 = mu * sigma2 * (cdf - a * pdf) + sigma * sigma2 * (a * a + 2.0f) * pdf;

    VisibleSlopeMoments moments;
    moments.mean = m1 / max(m0, 1.0e-8f);
    moments.std = sqrt(max(m2 / max(m0, 1.0e-8f) - moments.mean * moments.mean, 0.0f));
    return moments;
}

/// @brief 見えている面で平均したフレネル反射率（Schlick）を返す
/// @param cosTheta      視線と面の法線のなす角の余弦
/// @param slopeVariance 軸ごとの傾きの分散 σ²
/// @param f0            垂直入射の反射率
/// @details 縦の傾きは平均 ± √3σ の 3 点（重み 2/3・1/6・1/6）で積分し、
///          横の傾きは二乗の平均を面の法線の長さへ入れる
float ComputeVisibleFacetFresnel(float cosTheta, float slopeVariance, float f0)
{
    const float c = saturate(cosTheta);
    const float s = sqrt(1.0f - c * c);
    const float sigma2 = max(slopeVariance, 0.0f);
    const VisibleSlopeMoments moments = ComputeVisibleSlopeMoments(c, sigma2);
    const float offsets[3] = { 0.0f, -1.7320508f, 1.7320508f };
    const float weights[3] = { 2.0f / 3.0f, 1.0f / 6.0f, 1.0f / 6.0f };
    float fresnel = 0.0f;
    [unroll]
    for (int i = 0; i < 3; ++i)
    {
        const float p = moments.mean + offsets[i] * moments.std;
        const float facetCos = saturate((c + p * s) / sqrt(1.0f + p * p + sigma2));
        fresnel += weights[i] * (f0 + (1.0f - f0) * pow(1.0f - facetCos, 5.0f));
    }
    return fresnel;
}

#endif // WATER_VISIBLE_SLOPE_INCLUDED
