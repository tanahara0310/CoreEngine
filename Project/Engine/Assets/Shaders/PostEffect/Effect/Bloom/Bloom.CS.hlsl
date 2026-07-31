// Bloom.CS.hlsl - ブルーム コンピュートシェーダー

#include "ColorSpace.hlsli" // Luminance

Texture2D<float4> gTexture : register(t0);
RWTexture2D<float4> gOutput : register(u0);

cbuffer BloomParams : register(b0)
{
    float threshold;   // 輝度閾値 (0.0-2.0)
    float intensity;   // ブルーム強度 (0.0-3.0)
    float blurRadius;  // ブラー半径 (0.5-5.0)
    float softKnee;    // ソフトニー (0.0-1.0)
};

cbuffer ScreenParams : register(b1)
{
    uint screenWidth;
    uint screenHeight;
    float2 pad;
};

static const uint kGroupSize = 8;

// ソフトスレッショルド
float SoftThreshold(float lum, float thresh, float knee)
{
    float k = thresh * knee;
    float soft = lum - thresh + k;
    soft = clamp(soft, 0.0f, 2.0f * k);
    soft = soft * soft / (4.0f * k + 0.00001f);
    float contribution = max(soft, lum - thresh);
    contribution /= max(lum, 0.00001f);
    return contribution;
}

[numthreads(kGroupSize, kGroupSize, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    if (dispatchId.x >= screenWidth || dispatchId.y >= screenHeight)
    {
        return;
    }

    float4 originalColor = gTexture.Load(int3(dispatchId.xy, 0));

    // ガウシアンブラーでブルームを生成
    float4 blurred = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float totalWeight = 0.0f;
    int kernelRadius = max(1, (int)(blurRadius * 2.0f));
    kernelRadius = min(kernelRadius, 5);

    [loop]
    for (int x = -kernelRadius; x <= kernelRadius; x++)
    {
        [loop]
        for (int y = -kernelRadius; y <= kernelRadius; y++)
        {
            int2 sampleCoord = clamp(
                (int2)dispatchId.xy + int2(x, y),
                int2(0, 0),
                int2((int)screenWidth - 1, (int)screenHeight - 1)
            );
            float dist = length(float2(x, y));
            float weight = exp(-dist * dist / (2.0f * blurRadius * blurRadius));
            blurred += gTexture.Load(int3(sampleCoord, 0)) * weight;
            totalWeight += weight;
        }
    }
    blurred /= totalWeight;

    // 輝度によるブルーム抽出
    float lum = Luminance(blurred.rgb);
    float bloomContrib = SoftThreshold(lum, threshold, softKnee);
    float3 bloomColor = blurred.rgb * bloomContrib;

    // 元画像に加算合成
    float3 finalColor = originalColor.rgb + bloomColor * intensity;
    gOutput[dispatchId.xy] = float4(finalColor, originalColor.a);
}
