// LensFlareBlur.CS.hlsl - 分離型ガウスブラー（1 方向）
// ゴースト/ハローの輪郭を柔らかくする。C++ 側から水平・垂直の 2 回ディスパッチされ、
// 方向は BlurDirection CB（b1）で切り替える。

#include "LensFlareCommon.hlsli"

Texture2D<float4> gFlareInput : register(t0);
RWTexture2D<float4> gFlareOutput : register(u0);
SamplerState gLinearClamp : register(s0);

cbuffer BlurDirection : register(b1)
{
    float2 gBlurDir; // (1,0)=水平, (0,1)=垂直
    float2 gBlurPad;
};

static const uint kGroupSize = 8;
static const int kMaxRadius = 8;

[numthreads(kGroupSize, kGroupSize, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    if (dispatchId.x >= gFlareWidth || dispatchId.y >= gFlareHeight)
    {
        return;
    }

    const float2 flareSize = float2(gFlareWidth, gFlareHeight);
    const float2 uv = (float2(dispatchId.xy) + 0.5f) / flareSize;
    const float2 texel = gBlurDir / flareSize;

    const float sigma = max(gBlurSigma, 0.1f);
    const int radius = clamp((int)ceil(sigma * 2.0f), 1, kMaxRadius);

    float3 sum = 0.0f;
    float totalWeight = 0.0f;

    [loop]
    for (int t = -radius; t <= radius; ++t)
    {
        const float w = exp(-(float)(t * t) / (2.0f * sigma * sigma));
        sum += gFlareInput.SampleLevel(gLinearClamp, uv + texel * (float)t, 0).rgb * w;
        totalWeight += w;
    }

    gFlareOutput[dispatchId.xy] = float4(sum / totalWeight, 1.0f);
}
