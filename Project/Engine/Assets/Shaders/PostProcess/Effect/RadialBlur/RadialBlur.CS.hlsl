// RadialBlur.CS.hlsl - ラジアルブラー コンピュートシェーダー

Texture2D<float4> gTexture : register(t0);
RWTexture2D<float4> gOutput : register(u0);

cbuffer RadialBlurParams : register(b0)
{
    float intensity;    // ブラー強度 (0.0-2.0)
    float sampleCount;  // サンプル数 (4.0-16.0)
    float centerX;      // ブラー中心X座標 (0.0-1.0)
    float centerY;      // ブラー中心Y座標 (0.0-1.0)
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
        (float)dispatchId.y / (float)screenHeight
    );
    float2 center = float2(centerX, centerY);
    float2 dir = uv - center;
    float dist = length(dir);
    float2 normalizedDir = dist > 0.0001f ? dir / dist : float2(0.0f, 0.0f);

    float4 color = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float totalWeight = 0.0f;
    int samples = max(1, (int)sampleCount);

    [loop]
    for (int i = 0; i < samples; i++)
    {
        float t = (float)i / (float)(samples - 1);
        float offset = intensity * dist * t;
        float2 sampleUV = uv - normalizedDir * offset;

        // 境界チェックしてテクセル座標に変換
        sampleUV = saturate(sampleUV);
        int2 sampleCoord = (int2)(sampleUV * float2(screenWidth, screenHeight));
        sampleCoord = clamp(sampleCoord, int2(0, 0), int2((int)screenWidth - 1, (int)screenHeight - 1));

        float weight = 1.0f - t;
        color += gTexture.Load(int3(sampleCoord, 0)) * weight;
        totalWeight += weight;
    }

    if (totalWeight > 0.0f)
    {
        color /= totalWeight;
    }
    else
    {
        color = gTexture.Load(int3(dispatchId.xy, 0));
    }

    gOutput[dispatchId.xy] = color;
}
