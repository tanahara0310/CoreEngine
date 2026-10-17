#include "Sprite.hlsli"
#include "ColorSpace.hlsli"

ConstantBuffer<Material> gMaterial : register(b0);

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);
SamplerState gPointSampler : register(s1);

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    
    // テクスチャのUV座標を変換（Object3Dシェーダーと全く同じ方式）
    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);

    // テクスチャをサンプリング（ポイントは最も近い画素をそのまま読む）
    float4 textureColor = (gMaterial.filterMode == 1)
        ? gTexture.Sample(gPointSampler, transformedUV.xy)
        : gTexture.Sample(gSampler, transformedUV.xy);
    // 指定色は sRGB として受け取り、リニアへ直して掛ける
    const float4 materialColor = float4(SRGBToLinear(gMaterial.color.rgb), gMaterial.color.a);
    output.color = materialColor * textureColor;
    
    return output;
}