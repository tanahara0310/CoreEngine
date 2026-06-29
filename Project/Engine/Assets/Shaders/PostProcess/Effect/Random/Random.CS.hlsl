// Random.CS.hlsl - ランダムノイズ コンピュートシェーダー

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

float Hash12(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}

float3 Hash32(float2 p)
{
    return float3(
        Hash12(p + float2(1.0f, 0.0f)),
        Hash12(p + float2(0.0f, 1.0f)),
        Hash12(p + float2(1.0f, 1.0f)));
}

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
    float luminance = dot(baseColor.rgb, float3(0.299f, 0.587f, 0.114f));

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
