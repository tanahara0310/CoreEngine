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

float4 SampleFFTOceanBilinear(Texture2D<float4> textureData, float2 worldXZ, float patchLength, uint resolution)
{
    const float2 uv = frac(worldXZ / patchLength + 0.5f.xx);
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

#endif // RT_WATER_SURFACE_COMMON_HLSLI
