// ============================================================
// DXR 水面共通サーフェス評価 (Gerstner)
// RTWaterRefraction.hlsl / RTWaterCaustics.hlsl から共有される
// 波オフセット・法線評価ロジックを一元化する。
// 将来 FFT Ocean 版を追加する際は、同じシグネチャ
// (EvaluateWaterOffset / EvaluateWaterNormal) を持つ
// 代替バックエンドに差し替えるだけで済むようにする。
// ============================================================
#ifndef RT_WATER_SURFACE_COMMON_HLSLI
#define RT_WATER_SURFACE_COMMON_HLSLI

struct WaveParam
{
    float2 direction;
    float amplitude;
    float wavelength;
    float speed;
    float steepness;
    float phaseOffset;
    float padding;
};

cbuffer WaterSurfaceData : register(b1)
{
    float gSurfaceWaterHeight;
    uint gSurfaceActiveWaveCount;
    float gSurfaceTime;
    uint gSurfaceSimulationType;
    WaveParam gSurfaceWaves[16];
};

static const uint kWaterSurfaceModelTypeGerstner = 0;
static const uint kWaterSurfaceModelTypeFFTOcean = 1;

float3 EvaluateWaterOffsetGerstner(float2 worldXZ)
{
    float3 totalOffset = float3(0.0f, 0.0f, 0.0f);

    [unroll]
    for (uint waveIndex = 0; waveIndex < 16; ++waveIndex)
    {
        if (waveIndex >= gSurfaceActiveWaveCount)
        {
            break;
        }

        WaveParam wave = gSurfaceWaves[waveIndex];
        float k = 2.0f * 3.14159265f / max(wave.wavelength, 1.0e-4f);
        float omega = wave.speed * k;
        float kA = max(k * wave.amplitude, 1.0e-4f);
        float safeSteepness = min(wave.steepness, 0.95f / kA);
        float phase = k * dot(wave.direction, worldXZ) + omega * gSurfaceTime + wave.phaseOffset;
        float sinP = sin(phase);
        float cosP = cos(phase);

        totalOffset.x += safeSteepness * wave.amplitude * wave.direction.x * cosP;
        totalOffset.y += wave.amplitude * sinP;
        totalOffset.z += safeSteepness * wave.amplitude * wave.direction.y * cosP;
    }

    return totalOffset;
}

float3 EvaluateWaterNormalGerstner(float2 worldXZ)
{
    float3 dPdX = float3(1.0f, 0.0f, 0.0f);
    float3 dPdZ = float3(0.0f, 0.0f, 1.0f);

    [unroll]
    for (uint waveIndex = 0; waveIndex < 16; ++waveIndex)
    {
        if (waveIndex >= gSurfaceActiveWaveCount)
        {
            break;
        }

        WaveParam wave = gSurfaceWaves[waveIndex];
        float k = 2.0f * 3.14159265f / max(wave.wavelength, 1.0e-4f);
        float omega = wave.speed * k;
        float kA = max(k * wave.amplitude, 1.0e-4f);
        float safeSteepness = min(wave.steepness, 0.95f / kA);
        float phase = k * dot(wave.direction, worldXZ) + omega * gSurfaceTime + wave.phaseOffset;
        float sinP = sin(phase);
        float cosP = cos(phase);
        float common = safeSteepness * wave.amplitude * k * sinP;
        float heightSlope = wave.amplitude * k * cosP;

        dPdX += float3(
            -common * wave.direction.x * wave.direction.x,
             heightSlope * wave.direction.x,
            -common * wave.direction.x * wave.direction.y);

        dPdZ += float3(
            -common * wave.direction.x * wave.direction.y,
             heightSlope * wave.direction.y,
            -common * wave.direction.y * wave.direction.y);
    }

    float3 normal = normalize(cross(dPdZ, dPdX));
    return normal.y < 0.0f ? -normal : normal;
}

float3 EvaluateWaterOffsetFFTOcean(float2 worldXZ)
{
    worldXZ = worldXZ;
    return float3(0.0f, 0.0f, 0.0f);
}

float3 EvaluateWaterNormalFFTOcean(float2 worldXZ)
{
    worldXZ = worldXZ;
    return float3(0.0f, 1.0f, 0.0f);
}

float3 EvaluateWaterOffset(float2 worldXZ)
{
    if (gSurfaceSimulationType == kWaterSurfaceModelTypeFFTOcean)
    {
        return EvaluateWaterOffsetFFTOcean(worldXZ);
    }

    return EvaluateWaterOffsetGerstner(worldXZ);
}

float3 EvaluateWaterNormal(float2 worldXZ)
{
    if (gSurfaceSimulationType == kWaterSurfaceModelTypeFFTOcean)
    {
        return EvaluateWaterNormalFFTOcean(worldXZ);
    }

    return EvaluateWaterNormalGerstner(worldXZ);
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

/// @brief ワールドXZ → FFT テクスチャ UV の写像を適用してバイリニアサンプリングする
/// @param uvScale  uv = worldXZ * uvScale + uvOffset の係数。
///                 ラスタ描画（FFTWater.VS の sampleUV = (world - translate)/ローカルサイズ + scale/2）と
///                 同一の写像を渡すこと。以前は worldXZ / patchLength + 0.5 の固定写像で、
///                 メッシュの位置・スケール・V反転を考慮していなかったため、RT が評価する波面と
///                 実際に描画されている波面の位相が一致せず、屈折レイの交点・法線がズレて
///                 depth mismatch フォールバック（水面の一部だけ色が変わる領域）の原因になっていた。
float4 SampleFFTOceanBilinear(Texture2D<float4> textureData, float2 worldXZ, float2 uvScale, float2 uvOffset, uint resolution)
{
    const float2 uv = frac(worldXZ * uvScale + uvOffset);
    const float resolutionF = (float)resolution;
    const float2 texelPos = uv * resolutionF - 0.5f.xx;
    const int2 baseCoord = int2(floor(texelPos));
    const float2 fracCoord = frac(texelPos);
    const int wrapResolution = (int)resolution;

    const uint2 p00 = uint2(WrapFFTOceanCoord(baseCoord.x, wrapResolution), WrapFFTOceanCoord(baseCoord.y, wrapResolution));
    const uint2 p10 = uint2(WrapFFTOceanCoord(baseCoord.x + 1, wrapResolution), WrapFFTOceanCoord(baseCoord.y, wrapResolution));
    const uint2 p01 = uint2(WrapFFTOceanCoord(baseCoord.x, wrapResolution), WrapFFTOceanCoord(baseCoord.y + 1, wrapResolution));
    const uint2 p11 = uint2(WrapFFTOceanCoord(baseCoord.x + 1, wrapResolution), WrapFFTOceanCoord(baseCoord.y + 1, wrapResolution));

    const float4 c00 = textureData.Load(int3(p00, 0));
    const float4 c10 = textureData.Load(int3(p10, 0));
    const float4 c01 = textureData.Load(int3(p01, 0));
    const float4 c11 = textureData.Load(int3(p11, 0));

    const float4 cx0 = lerp(c00, c10, fracCoord.x);
    const float4 cx1 = lerp(c01, c11, fracCoord.x);
    return lerp(cx0, cx1, fracCoord.y);
}

// ============================================================
// マルチスケール・カスケード（Texture2DArray）
// ラスタ描画（FFTWater.VS / Water.PS）と同一の「回転格子系ワールドXZ / パッチ長」写像で
// 各スライスをサンプルして合算する。FFTOceanManager kCascadePatchLength / kCascadeRotCos/Sin
// と一致必須（パッチ長は互いに素な素数、格子回転 0°/+26°/-49°）。
// ============================================================
static const int kFFTCascadeCount = 3;
static const float kFFTCascadePatch[3] = { 521.0f, 127.0f, 31.0f };
static const float kFFTCascadeRotC[3] = { 1.0f, 0.89879405f, 0.65605903f };
static const float kFFTCascadeRotS[3] = { 0.0f, 0.43837115f, -0.75471006f };

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

// 波群エンベロープ（タイル周期破壊の空間振幅変調）。
// FFTWater.VS / Water.PS と完全一致必須（詳細コメントは FFTWater.VS 参照）。
static const float kFFTWaveGroupStrength = 0.12f;
float ComputeFFTWaveGroupEnvelope(float2 worldXZ)
{
    float g = sin(dot(worldXZ, float2(0.01071f, 0.01353f)) + 0.917f)
            + sin(dot(worldXZ, float2(-0.01409f, 0.00893f)) + 2.618f)
            + sin(dot(worldXZ, float2(0.00531f, -0.00713f)) + 4.523f);
    return 1.0f + kFFTWaveGroupStrength * g;
}

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
