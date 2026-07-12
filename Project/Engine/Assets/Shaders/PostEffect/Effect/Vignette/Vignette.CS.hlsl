// Vignette.CS.hlsl - ビネット コンピュートシェーダー

Texture2D<float4> gTexture : register(t0);
RWTexture2D<float4> gOutput : register(u0);

cbuffer VignetteParams : register(b0)
{
    float intensity;   // ビネット強度
    float smoothness;  // 滑らかさ
    float size;        // サイズ
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

    float4 color = gTexture.Load(int3(dispatchId.xy, 0));

    // UV 座標（0~1）
    float2 uv = float2(
        (float)dispatchId.x / (float)screenWidth,
        (float)dispatchId.y / (float)screenHeight
    );

    // 周辺ほど暗くなるビネット計算
    float2 correct = uv * (1.0f - uv.yx);
    float vignette = correct.x * correct.y * size;
    vignette = saturate(pow(vignette, smoothness));
    vignette = lerp(0.0f, vignette, intensity);

    color.rgb *= vignette;

    gOutput[dispatchId.xy] = color;
}
