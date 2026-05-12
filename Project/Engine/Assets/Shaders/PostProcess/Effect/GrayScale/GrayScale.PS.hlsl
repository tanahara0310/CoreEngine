Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

struct PixelShaderOutput
{
    float4 color : SV_Target;
};

PixelShaderOutput main(PixelShaderInput input)
{
    PixelShaderOutput output;

    // サンプル
    output.color = gTexture.Sample(gSampler, input.texcoord);

    // Rec.709 輝度係数（0.2125, 0.7154, 0.0721）
    float value = dot(output.color.rgb, float3(0.2125f, 0.7154f, 0.0721f));

    // RGB を同値で上書き（αは維持）
    output.color.rgb = float3(value, value, value);
    
    //output.color.rgb = value * float3(1.0f, 74.0f / 107.0f, 43.0f / 107.0f);

    return output;
}