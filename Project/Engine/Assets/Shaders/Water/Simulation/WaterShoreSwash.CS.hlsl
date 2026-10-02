// ============================================================
// 岸の泡の寄せ・引きのずれ
// ============================================================

#include "../Common/FFTOceanCascade.hlsli"

Texture2D<float> gSmoothSeabedHeight : register(t0);
Texture2DArray<float4> gFFTOceanDisplacement : register(t1);
RWTexture2D<float2> gSwashOutput : register(u0);
SamplerState gLinearWrap : register(s0);

cbuffer WaterShoreSwashConstants : register(b0)
{
    float2 gWindowOriginXZ;  // 範囲の XZ の最小の角 [m]
    float gWindowSize;       // 範囲の一辺 [m]
    uint gResolution;        // ならした海底の高さと出力の一辺のテクセル数
    float gWaterRestHeight;  // 静水面の高さ [m]
    float3 gWaveGroupPhase;  // 波群エンベロープの位相のずれ [rad]
};

/// @brief これより緩い海底の上では寄せ・引きでずらさない（勾配）
static const float kSwashMinSlope = 0.04f;
/// @brief 寄せ・引きのずれを弱め始める静水深と、0 にする静水深 [m]
static const float kSwashFullDepth = 0.5f;
static const float kSwashEndDepth = 1.5f;
/// @brief 寄せ・引きのずれの上限 [m]
static const float kMaxSwashMeters = 3.0f;
/// @brief ずれをならす範囲の半径 [テクセル] と、ガウスの広がり [テクセル]
static const int kBlurRadius = 2;
static const float kBlurSigma = 1.2f;

/// @brief 水面の上下（静水面からの高さ）[m]。水面メッシュの頂点と同じ式
float SampleWaveElevation(float2 worldXZ)
{
    float height = 0.0f;
    [unroll]
    for (int c = 0; c < kFFTGeometryCascadeCount; ++c)
    {
        const float2 cuv = ComputeFFTCascadeUV(worldXZ, c);
        height += gFFTOceanDisplacement.SampleLevel(gLinearWrap, float3(cuv, (float)c), 0.0f).y;
    }
    return height * ComputeFFTWaveGroupEnvelope(worldXZ, gWaveGroupPhase);
}

/// @brief 1 テクセルの中の水面の上下の平均 [m]（3×3 点。うねりのような長い波だけを残す）
float SampleTexelElevation(float2 centerXZ, float texelSize)
{
    float sum = 0.0f;
    [unroll]
    for (int j = -1; j <= 1; ++j)
    {
        [unroll]
        for (int i = -1; i <= 1; ++i)
        {
            sum += SampleWaveElevation(centerXZ + float2(i, j) * (texelSize / 3.0f));
        }
    }
    return sum / 9.0f;
}

/// @brief ならした海底の高さ（範囲の外は端の値）
float LoadSmoothHeight(int2 coord)
{
    return gSmoothSeabedHeight.Load(int3(clamp(coord, 0, (int)gResolution - 1), 0));
}

/// @brief 1 テクセルの寄せ・引きのずれと、その値を使うかの重み（ずれ x, z に重みを掛けた値, 重み）
/// @details 浜では水面が上がった分だけ水が坂を上るので、ずれは「水面の上下 ÷ 勾配」で、向きは上り坂。
///          重みは、坂になっていて水際に近い所だけ 1 にする
float3 ComputeWeightedSwash(int2 coord, float texelSize)
{
    const float2 gradient = float2(
        LoadSmoothHeight(coord + int2(1, 0)) - LoadSmoothHeight(coord - int2(1, 0)),
        LoadSmoothHeight(coord + int2(0, 1)) - LoadSmoothHeight(coord - int2(0, 1))) / (2.0f * texelSize);
    const float slope = length(gradient);
    const float stillDepth = gWaterRestHeight - LoadSmoothHeight(coord);
    const float weight = smoothstep(kSwashMinSlope, 2.0f * kSwashMinSlope, slope)
        * (1.0f - smoothstep(kSwashFullDepth, kSwashEndDepth, stillDepth));

    // 上限 kMaxSwashMeters へなだらかに頭打ちさせる
    const float2 centerXZ = gWindowOriginXZ + (float2(coord) + 0.5f) * texelSize;
    const float ratio = SampleTexelElevation(centerXZ, texelSize) / (max(slope, kSwashMinSlope) * kMaxSwashMeters);
    const float distance = kMaxSwashMeters * ratio * rsqrt(1.0f + ratio * ratio);
    const float2 swash = gradient / max(slope, 1.0e-4f) * distance;
    return float3(swash * weight, weight);
}

/// @brief 周りの重みつきの平均で寄せ・引きのずれをならして書く
/// @details 重みのある所（水際の坂）の値で平均するので、陸の側へは水際の値が延びる。
///          周りに重みのある所が少ないほど 0 へ寄せる
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= gResolution || dispatchThreadId.y >= gResolution)
    {
        return;
    }

    const float texelSize = gWindowSize / (float)gResolution;
    float2 weightedSum = float2(0.0f, 0.0f);
    float weightSum = 0.0f;
    float kernelSum = 0.0f;
    for (int j = -kBlurRadius; j <= kBlurRadius; ++j)
    {
        for (int i = -kBlurRadius; i <= kBlurRadius; ++i)
        {
            const float kernel = exp(-(float)(i * i + j * j) / (2.0f * kBlurSigma * kBlurSigma));
            const float3 weighted = ComputeWeightedSwash(int2(dispatchThreadId.xy) + int2(i, j), texelSize);
            weightedSum += weighted.xy * kernel;
            weightSum += weighted.z * kernel;
            kernelSum += kernel;
        }
    }

    const float coverage = saturate(2.0f * weightSum / kernelSum);
    gSwashOutput[dispatchThreadId.xy] = weightedSum / max(weightSum, 1.0e-4f) * coverage;
}
