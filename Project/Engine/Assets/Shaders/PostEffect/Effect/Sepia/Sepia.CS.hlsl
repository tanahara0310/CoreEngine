// Sepia.CS.hlsl - セピア コンピュートシェーダー

Texture2D<float4> gTexture : register(t0);
RWTexture2D<float4> gOutput : register(u0);

cbuffer SepiaParams : register(b0)
{
    float intensity;   // セピア効果の強度
    float toneRed;     // 赤色調整
    float toneGreen;   // 緑色調整
    float toneBlue;    // 青色調整
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

    float4 color = gTexture.Load(int3(dispatchId.xy, 0));

    // グレースケール輝度計算
    float luminance = dot(color.rgb, float3(0.299f, 0.587f, 0.114f));

    // セピアトーン適用
    float3 sepiaColor = float3(
        luminance * toneRed,
        luminance * toneGreen,
        luminance * toneBlue
    );

    color.rgb = lerp(color.rgb, sepiaColor, intensity);

    gOutput[dispatchId.xy] = color;
}
