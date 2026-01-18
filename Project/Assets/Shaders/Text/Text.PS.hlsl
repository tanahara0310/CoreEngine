#include "Text.hlsli"

ConstantBuffer<Material> gMaterial : register(b0);

Texture2D<float> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    
    float alpha = gTexture.Sample(gSampler, input.texcoord);
    
    output.color = float4(gMaterial.color.rgb, gMaterial.color.a * alpha);
    
    // アンチエイリアシングの境界を少し強めにカット
    if (output.color.a < 0.05f)
    {
        discard;
    }
    
    return output;
}
