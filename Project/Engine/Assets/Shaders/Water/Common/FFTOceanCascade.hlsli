// ============================================================
// FFT Ocean カスケードの共通定数とサンプリング写像（HLSL 側）
// ------------------------------------------------------------
// 数値そのものは FFTOceanCascadeValues.hlsli（C++ とも共有）が唯一の情報源。
// このファイルはそれを HLSL の型付き定数へ展開し、
// 「ワールドXZ ⇄ 回転格子系」の写像と波群エンベロープを提供する。
//
// 利用側: FFTWater.VS / Water.PS / RTWaterSurfaceCommon（→ RT 3 本）
// ここを通さずに写像を手書きすると、ラスタ描画と RT の波面がズレる。
// ============================================================
#ifndef FFT_OCEAN_CASCADE_INCLUDED
#define FFT_OCEAN_CASCADE_INCLUDED

#include "FFTOceanCascadeValues.hlsli"

static const int kFFTCascadeCount = FFT_OCEAN_CASCADE_COUNT;
static const float kFFTCascadePatch[FFT_OCEAN_CASCADE_COUNT] = FFT_OCEAN_CASCADE_PATCH_LENGTHS;
static const float kFFTCascadeRotC[FFT_OCEAN_CASCADE_COUNT] = FFT_OCEAN_CASCADE_ROT_COS;
static const float kFFTCascadeRotS[FFT_OCEAN_CASCADE_COUNT] = FFT_OCEAN_CASCADE_ROT_SIN;

// 頂点変位に使うカスケード数（詳細は FFTOceanCascadeValues.hlsli）
static const int kFFTGeometryCascadeCount = FFT_OCEAN_GEOMETRY_CASCADE_COUNT;

static const float kFFTWaveGroupStrength = FFT_OCEAN_WAVE_GROUP_STRENGTH;

/// @brief ワールドXZ を指定カスケードの回転格子系へ変換する
float2 RotateToFFTCascadeGrid(float2 worldXZ, int cascade)
{
    const float rc = kFFTCascadeRotC[cascade];
    const float rs = kFFTCascadeRotS[cascade];
    return float2(rc * worldXZ.x - rs * worldXZ.y, rs * worldXZ.x + rc * worldXZ.y);
}

/// @brief 回転格子系の水平ベクトル（変位・傾き）をワールドへ逆回転する
float2 RotateFromFFTCascadeGrid(float2 gridVector, int cascade)
{
    const float rc = kFFTCascadeRotC[cascade];
    const float rs = kFFTCascadeRotS[cascade];
    return float2(rc * gridVector.x + rs * gridVector.y, -rs * gridVector.x + rc * gridVector.y);
}

/// @brief ワールドXZ → 指定カスケードのテクスチャ UV（回転 ＋ パッチ長で正規化）
float2 ComputeFFTCascadeUV(float2 worldXZ, int cascade)
{
    return RotateToFFTCascadeGrid(worldXZ, cascade) / kFFTCascadePatch[cascade];
}

/// @brief 波群エンベロープ（タイル周期を崩す空間振幅変調）
/// @details 変位（FFTWater.VS）と傾き（Water.PS / RT）へ同じ係数を掛けること。
///          片方だけに掛けると幾何と法線が食い違う。
float ComputeFFTWaveGroupEnvelope(float2 worldXZ)
{
    float g = sin(dot(worldXZ, float2(0.01071f, 0.01353f)) + 0.917f)
            + sin(dot(worldXZ, float2(-0.01409f, 0.00893f)) + 2.618f)
            + sin(dot(worldXZ, float2(0.00531f, -0.00713f)) + 4.523f);
    return 1.0f + kFFTWaveGroupStrength * g;
}

/// @brief 全カスケードのヤコビアン勾配をワールド系で合成し、変位後サーフェスの detJ を返す
/// @param worldXZ        評価点のワールド XZ
/// @param jacobianTex    FFTOceanFinalize.CS 出力。各スライス = (Jxx, Jzz, Jxy, detJ_cascade)
/// @param samp           WRAP サンプラ（カスケードのワールドタイリング用。gSampler と同等のもの）
/// @param cascadeWeights カスケード別の勾配寄与の重み [0,1]。勾配の大きさは波数 k に
///                       比例するため、無重み（1,1,1）では最小パッチ（31m のさざ波）が
///                       合成を支配し、海面の広範囲で detJ ≤ 0 に飽和してしまう
///                       （2026-08-02 Phase 0 実測）。泡の見た目調整はこの重みで行う。
/// @details 泡（whitecap）の発生判定 detJ = det(I + ∂D/∂x) の正準評価関数。
///          カスケード単体の detJ(.w) は和に分配されないため合成には使えない。
///          各カスケードの勾配テンソル J（対称 2×2: [Jxx Jxy; Jxy Jzz]）を
///          回転共役 Rᵀ·J·R でワールド系へ揃えてから合算する
///          （変位ベクトルの RotateFromFFTCascadeGrid と同じ回転の、テンソル版）。
///          変位（FFTWater.VS）に掛かる波群エンベロープは勾配にも同倍率で掛かるため
///          ここで乗算する（エンベロープ自身の空間勾配は波長 364m〜 なので無視できる）。
///          detJ < 1 が波頭の圧縮、detJ < 0 が折り返し（foldover ＝ 砕波）。
float ComputeFFTCombinedDetJ(
    float2 worldXZ, Texture2DArray<float4> jacobianTex, SamplerState samp, float3 cascadeWeights)
{
    float jxx = 0.0f;
    float jzz = 0.0f;
    float jxy = 0.0f;
    [unroll]
    for (int ci = 0; ci < kFFTCascadeCount; ++ci)
    {
        const float2 cuv = ComputeFFTCascadeUV(worldXZ, ci);
        const float w = cascadeWeights[ci];
        const float3 j = jacobianTex.SampleLevel(samp, float3(cuv, (float)ci), 0.0f).xyz * w;
        const float rc = kFFTCascadeRotC[ci];
        const float rs = kFFTCascadeRotS[ci];
        // Rᵀ·J·R の展開（j.x=Jxx, j.y=Jzz, j.z=Jxy）
        jxx += j.x * rc * rc + 2.0f * j.z * rc * rs + j.y * rs * rs;
        jzz += j.x * rs * rs - 2.0f * j.z * rc * rs + j.y * rc * rc;
        jxy += j.z * (rc * rc - rs * rs) + (j.y - j.x) * rc * rs;
    }

    const float envelope = ComputeFFTWaveGroupEnvelope(worldXZ);
    jxx *= envelope;
    jzz *= envelope;
    jxy *= envelope;

    return (1.0f + jxx) * (1.0f + jzz) - jxy * jxy;
}

#endif // FFT_OCEAN_CASCADE_INCLUDED
