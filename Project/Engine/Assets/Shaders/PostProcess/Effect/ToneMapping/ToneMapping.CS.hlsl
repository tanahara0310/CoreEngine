// ToneMapping.CS.hlsl - ACES トーンマッピング コンピュートシェーダー

Texture2D<float4> gTexture : register(t0);
RWTexture2D<float4> gOutput : register(u0);

cbuffer ScreenParams : register(b0)
{
    uint screenWidth;
    uint screenHeight;
    float2 pad;
};

static const uint kGroupSize = 8;

// ACES トーンマッピング近似
float3 ACESFilm(float3 x)
{
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

[numthreads(kGroupSize, kGroupSize, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    if (dispatchId.x >= screenWidth || dispatchId.y >= screenHeight)
    {
        return;
    }

    float4 color = gTexture.Load(int3(dispatchId.xy, 0));

    // HDR → LDR 変換
    color.rgb = ACESFilm(color.rgb);

    // ガンマ補正（リニア → sRGB）
    color.rgb = pow(saturate(color.rgb), 1.0f / 2.2f);

    gOutput[dispatchId.xy] = color;
}
