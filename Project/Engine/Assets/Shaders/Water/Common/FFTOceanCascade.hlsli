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

#endif // FFT_OCEAN_CASCADE_INCLUDED
