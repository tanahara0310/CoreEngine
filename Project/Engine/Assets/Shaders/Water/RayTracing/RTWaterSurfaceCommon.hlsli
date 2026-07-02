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

#endif // RT_WATER_SURFACE_COMMON_HLSLI
