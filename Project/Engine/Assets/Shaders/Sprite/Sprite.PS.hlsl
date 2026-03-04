#include "Sprite.hlsli"

ConstantBuffer<Material> gMaterial : register(b0);

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    
    // テクスチャのUV座標を変換（Object3Dシェーダーと全く同じ方式）
    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    
    // テクスチャをサンプリング
    float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);
    output.color = gMaterial.color * textureColor;
    
    return output;
}