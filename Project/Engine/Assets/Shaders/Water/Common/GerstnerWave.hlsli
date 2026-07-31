// ============================================================
// Gerstner Wave の共通評価（波 1 本分の数式）
// ------------------------------------------------------------
// 同じ式が RTWaterSurfaceCommon.hlsli（DXR・b1）と WaterCaustics.PS.hlsl
// （スクリーンスペース・b2）に別々に書かれていた。片方だけ直すと水面描画と
// 集光模様の波面がズレる（「基準不一致」型の不具合）。
//
// cbuffer のレイアウトは用途ごとに異なる（DXR は simulationType、コースティクスは
// 水域矩形を持つ）ため、cbuffer 宣言とループは各シェーダー側に残し、
// 「ズレると壊れる数式」だけをここへ集約する。
//
// C++ 側の対応型: CoreEngine::WaterWaveParam / WaveParams（いずれも 32 バイト）
// ============================================================
#ifndef GERSTNER_WAVE_INCLUDED
#define GERSTNER_WAVE_INCLUDED

#include "ShaderMath.hlsli" // TWO_PI

// 定数バッファに格納できる Gerstner 波の最大本数。
// C++ 側 kMaxWaterWaveCount / kMaxWaterSurfaceWaveCount と一致させること。
#define GERSTNER_MAX_WAVE_COUNT 16
static const uint kMaxGerstnerWaveCount = GERSTNER_MAX_WAVE_COUNT;

/// @brief Gerstner 波 1 本分のパラメータ（32 バイト）
/// @note C++ 側 WaveParams / WaterWaveParam とメモリレイアウトを一致させること
struct GerstnerWave
{
    float2 direction;
    float amplitude;
    float wavelength;
    float speed;
    float steepness;
    float phaseOffset;
    float padding;
};

/// @brief 波数 k・角周波数 ω・自己交差を避けた実効 steepness・位相をまとめて求める
/// @details 尖りすぎ（kA が大きい）と Gerstner 波は自己交差してループを作るため、
///          steepness を 0.95/kA で頭打ちにする。この抑制を片方の実装だけに入れると
///          荒れた海で波面が食い違うので、必ずこの関数を経由すること。
void ResolveGerstnerWaveTerms(
    GerstnerWave wave, float time, float2 worldXZ,
    out float waveNumber, out float safeSteepness, out float sinPhase, out float cosPhase)
{
    waveNumber = TWO_PI / max(wave.wavelength, 1.0e-4f);
    const float angularFrequency = wave.speed * waveNumber;
    const float kA = max(waveNumber * wave.amplitude, 1.0e-4f);
    safeSteepness = min(wave.steepness, 0.95f / kA);

    const float phase = waveNumber * dot(wave.direction, worldXZ)
        + angularFrequency * time + wave.phaseOffset;
    sinPhase = sin(phase);
    cosPhase = cos(phase);
}

/// @brief 波 1 本分の頂点変位（XZ の横ずれ ＋ Y の高さ）
float3 EvaluateGerstnerWaveOffset(GerstnerWave wave, float time, float2 worldXZ)
{
    float waveNumber;
    float safeSteepness;
    float sinPhase;
    float cosPhase;
    ResolveGerstnerWaveTerms(wave, time, worldXZ, waveNumber, safeSteepness, sinPhase, cosPhase);

    // 乗算順序は元の実装（RTWaterSurfaceCommon / WaterCaustics.PS）のまま維持する。
    // -Od では再結合されないため、順序を変えると最下位ビットがズレて
    // 「同じ波面を別々に評価した結果」がわずかに食い違う余地を作ってしまう。
    return float3(
        safeSteepness * wave.amplitude * wave.direction.x * cosPhase,
        wave.amplitude * sinPhase,
        safeSteepness * wave.amplitude * wave.direction.y * cosPhase);
}

/// @brief 波 1 本分の接ベクトル微分寄与を積算する
/// @param dPdX ワールド X 方向の接ベクトル（呼び出し前に float3(1,0,0) で初期化）
/// @param dPdZ ワールド Z 方向の接ベクトル（呼び出し前に float3(0,0,1) で初期化）
void AccumulateGerstnerWaveDerivatives(
    GerstnerWave wave, float time, float2 worldXZ, inout float3 dPdX, inout float3 dPdZ)
{
    float waveNumber;
    float safeSteepness;
    float sinPhase;
    float cosPhase;
    ResolveGerstnerWaveTerms(wave, time, worldXZ, waveNumber, safeSteepness, sinPhase, cosPhase);

    const float common = safeSteepness * wave.amplitude * waveNumber * sinPhase;
    const float heightSlope = wave.amplitude * waveNumber * cosPhase;

    dPdX += float3(
        -common * wave.direction.x * wave.direction.x,
         heightSlope * wave.direction.x,
        -common * wave.direction.x * wave.direction.y);

    dPdZ += float3(
        -common * wave.direction.x * wave.direction.y,
         heightSlope * wave.direction.y,
        -common * wave.direction.y * wave.direction.y);
}

/// @brief 積算した接ベクトルから上向きのワールド法線を作る
float3 BuildGerstnerNormal(float3 dPdX, float3 dPdZ)
{
    const float3 normal = normalize(cross(dPdZ, dPdX));
    return normal.y < 0.0f ? -normal : normal;
}

#endif // GERSTNER_WAVE_INCLUDED
