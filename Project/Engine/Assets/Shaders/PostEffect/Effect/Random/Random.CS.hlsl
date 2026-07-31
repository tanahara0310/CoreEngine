// Random.CS.hlsl - ランダムノイズ コンピュートシェーダー

#include "ColorSpace.hlsli" // LuminanceRec601
#include "Hash.hlsli"       // Hash12 / Hash32

Texture2D<float4> gTexture : register(t0);
RWTexture2D<float4> gOutput : register(u0);

cbuffer RandomParams : register(b0)
{
    float intensity;
    float blend;
    float speed;
    float time;

    float grainScale;
    float luminanceInfluence;
    float chromaAmount;
    float padding;
};

cbuffer ScreenParams : register(b1)
{
    uint screenWidth;
    uint screenHeight;
    float2 pad;
};

static const uint kGroupSize = 8;

[numthreads(kGroupSize, kGroupSize, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    if (dispatchId.x >= screenWidth || dispatchId.y >= screenHeight)
    {
        return;
    }

    float2 uv = float2(
        (float)dispatchId.x / (float)screenWidth,
        (float)dispatchId.y / (float)screenHeight);

    float4 baseColor = gTexture.Load(int3(dispatchId.xy, 0));
    float luminance = LuminanceRec601(baseColor.rgb);

    float2 noiseCoord = uv * float2(screenWidth, screenHeight) * grainScale + time * float2(17.0f, 29.0f);
    float noise = Hash12(noiseCoord) * 2.0f - 1.0f;
    float3 colorNoise = Hash32(noiseCoord + 13.0f) * 2.0f - 1.0f;

    float luminanceFactor = lerp(1.0f, saturate(1.0f - luminance), luminanceInfluence);
    float monoNoise = noise * intensity * luminanceFactor;
    float3 grain = monoNoise.xxx;
    grain += colorNoise * (intensity * chromaAmount * 0.5f);

    float3 noisyColor = saturate(baseColor.rgb + grain);
    float3 finalColor = lerp(baseColor.rgb, noisyColor, saturate(blend));

    gOutput[dispatchId.xy] = float4(finalColor, baseColor.a);
}
