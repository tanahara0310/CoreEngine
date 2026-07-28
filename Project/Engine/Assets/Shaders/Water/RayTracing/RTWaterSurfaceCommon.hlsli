// ============================================================
// DXR 水面共通サーフェス評価
// RTWaterRefraction / RTWaterReflection / RTWaterCaustics から共有する。
//   - Gerstner 経路: EvaluateWaterOffsetGerstner / EvaluateWaterNormalGerstner
//   - FFT Ocean 経路: SampleFFTOceanCascadeDisplacement / SampleFFTOceanCascadeNormal
// どちらを使うかは各シェーダーの IsFFTOceanSurfaceActive() が判定する
// （以前はここに simulationType 分岐と平坦を返す FFT スタブがあり、
//   呼ぶと「波の無い水面」になる罠だったため撤去した）。
// ============================================================
#ifndef RT_WATER_SURFACE_COMMON_HLSLI
#define RT_WATER_SURFACE_COMMON_HLSLI

#include "../Common/GerstnerWave.hlsli"
#include "../Common/FFTOceanCascade.hlsli"

cbuffer WaterSurfaceData : register(b1)
{
    float gSurfaceWaterHeight;
    uint gSurfaceActiveWaveCount;
    float gSurfaceTime;
    uint gSurfaceSimulationType;
    GerstnerWave gSurfaceWaves[GERSTNER_MAX_WAVE_COUNT];
};

static const uint kWaterSurfaceModelTypeGerstner = 0;
static const uint kWaterSurfaceModelTypeFFTOcean = 1;

float3 EvaluateWaterOffsetGerstner(float2 worldXZ)
{
    float3 totalOffset = float3(0.0f, 0.0f, 0.0f);

    [unroll]
    for (uint waveIndex = 0; waveIndex < kMaxGerstnerWaveCount; ++waveIndex)
    {
        if (waveIndex >= gSurfaceActiveWaveCount)
        {
            break;
        }

        totalOffset += EvaluateGerstnerWaveOffset(gSurfaceWaves[waveIndex], gSurfaceTime, worldXZ);
    }

    return totalOffset;
}

float3 EvaluateWaterNormalGerstner(float2 worldXZ)
{
    float3 dPdX = float3(1.0f, 0.0f, 0.0f);
    float3 dPdZ = float3(0.0f, 0.0f, 1.0f);

    [unroll]
    for (uint waveIndex = 0; waveIndex < kMaxGerstnerWaveCount; ++waveIndex)
    {
        if (waveIndex >= gSurfaceActiveWaveCount)
        {
            break;
        }

        AccumulateGerstnerWaveDerivatives(
            gSurfaceWaves[waveIndex], gSurfaceTime, worldXZ, dPdX, dPdZ);
    }

    return BuildGerstnerNormal(dPdX, dPdZ);
}

// ------------------------------------------------------------
// FFT Ocean テクスチャ（変位・法線）のバイリニアサンプリング
// FFT Ocean はテクセル解像度が有限のため、Load によるニアレストサンプリングでは
// テクセル境界で法線が階段状に変化し、屈折方向がテクセル単位でジャンプする。
// このジャンプは再投影 UV の飛びとなり、画面上に FFT 解像度グリッドに一致した
// 四角いモザイク状の破綻として現れる。RTWaterRefraction / RTWaterCaustics の
// 両方で同じバイリニア補間を使い、この量子化を避ける。
// ------------------------------------------------------------
uint WrapFFTOceanCoord(int coord, int resolution)
{
    int wrapped = coord % resolution;
    if (wrapped < 0)
    {
        wrapped += resolution;
    }

    return (uint)wrapped;
}

// ============================================================
// マルチスケール・カスケード（Texture2DArray）
// ラスタ描画（FFTWater.VS / Water.PS）と同一の「回転格子系ワールドXZ / パッチ長」写像で
// 各スライスをサンプルして合算する。定数と写像は Common/FFTOceanCascade.hlsli が唯一の情報源。
// ============================================================

/// @brief 配列テクスチャの1スライスをワールドXZ/パッチ長でバイリニアサンプルする
float4 SampleFFTOceanArraySlice(Texture2DArray<float4> textureData, float2 worldXZ, float patchLength, uint slice, uint resolution)
{
    const float2 uv = frac(worldXZ / patchLength);
    const float resolutionF = (float)resolution;
    const float2 texelPos = uv * resolutionF - 0.5f.xx;
    const int2 baseCoord = int2(floor(texelPos));
    const float2 fracCoord = frac(texelPos);
    const int wrapResolution = (int)resolution;

    const uint2 p00 = uint2(WrapFFTOceanCoord(baseCoord.x, wrapResolution), WrapFFTOceanCoord(baseCoord.y, wrapResolution));
    const uint2 p10 = uint2(WrapFFTOceanCoord(baseCoord.x + 1, wrapResolution), WrapFFTOceanCoord(baseCoord.y, wrapResolution));
    const uint2 p01 = uint2(WrapFFTOceanCoord(baseCoord.x, wrapResolution), WrapFFTOceanCoord(baseCoord.y + 1, wrapResolution));
    const uint2 p11 = uint2(WrapFFTOceanCoord(baseCoord.x + 1, wrapResolution), WrapFFTOceanCoord(baseCoord.y + 1, wrapResolution));

    const float4 c00 = textureData.Load(int4(p00, slice, 0));
    const float4 c10 = textureData.Load(int4(p10, slice, 0));
    const float4 c01 = textureData.Load(int4(p01, slice, 0));
    const float4 c11 = textureData.Load(int4(p11, slice, 0));

    const float4 cx0 = lerp(c00, c10, fracCoord.x);
    const float4 cx1 = lerp(c01, c11, fracCoord.x);
    return lerp(cx0, cx1, fracCoord.y);
}

/// @brief 全カスケードの変位を合算する
float3 SampleFFTOceanCascadeDisplacement(Texture2DArray<float4> textureData, float2 worldXZ, uint resolution)
{
    float3 displacement = float3(0.0f, 0.0f, 0.0f);
    [unroll]
    for (int c = 0; c < kFFTCascadeCount; ++c)
    {
        const float2 gridXZ = RotateToFFTCascadeGrid(worldXZ, c);
        const float3 d = SampleFFTOceanArraySlice(textureData, gridXZ, kFFTCascadePatch[c], (uint)c, resolution).xyz;
        const float2 horizontal = RotateFromFFTCascadeGrid(float2(d.x, d.z), c);
        displacement += float3(horizontal.x, d.y, horizontal.y);
    }
    // 波群エンベロープでタイル周期を崩す（ラスタ描画と同一の変調）
    return displacement * ComputeFFTWaveGroupEnvelope(worldXZ);
}

/// @brief 全カスケードの法線（傾き）を合算したワールド法線を返す
float3 SampleFFTOceanCascadeNormal(Texture2DArray<float4> textureData, float2 worldXZ, uint resolution)
{
    float2 slope = float2(0.0f, 0.0f);
    [unroll]
    for (int c = 0; c < kFFTCascadeCount; ++c)
    {
        const float2 gridXZ = RotateToFFTCascadeGrid(worldXZ, c);
        const float3 enc = SampleFFTOceanArraySlice(textureData, gridXZ, kFFTCascadePatch[c], (uint)c, resolution).xyz;
        const float3 nLocal = normalize(enc * 2.0f - 1.0f);
        // 回転格子系の傾きをワールドへ逆回転してから合算する
        slope += RotateFromFFTCascadeGrid(nLocal.xz / max(nLocal.y, 1.0e-3f), c);
    }
    // 波群エンベロープ: 変位と同じ変調を傾きへ掛けて波面の幾何と一致させる
    slope *= ComputeFFTWaveGroupEnvelope(worldXZ);
    float3 n = normalize(float3(slope.x, 1.0f, slope.y));
    return n.y < 0.0f ? -n : n;
}

#endif // RT_WATER_SURFACE_COMMON_HLSLI
