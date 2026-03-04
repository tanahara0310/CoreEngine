#include "Skybox.hlsli"

TextureCube<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct Material
{
    float4 color;
};

ConstantBuffer<Material> gMaterial : register(b0);

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    
    // キューブマップテクスチャをサンプリング
    float4 textureColor = gTexture.Sample(gSampler, input.texcoord);
    
    // マテリアルカラーと乗算
    output.color = gMaterial.color * textureColor;
    
    return output;
}
