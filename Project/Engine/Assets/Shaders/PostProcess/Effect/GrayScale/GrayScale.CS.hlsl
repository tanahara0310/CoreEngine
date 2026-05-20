// GrayScale.CS.hlsl - グレースケール コンピュートシェーダー

Texture2D<float4> gTexture : register(t0);
RWTexture2D<float4> gOutput : register(u0);

cbuffer ScreenParams : register(b0)
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

    // Rec.709 輝度係数
    float value = dot(color.rgb, float3(0.2125f, 0.7154f, 0.0721f));
    color.rgb = float3(value, value, value);

    gOutput[dispatchId.xy] = color;
}
