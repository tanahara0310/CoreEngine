#include "UI.hlsli"
#include "ColorSpace.hlsli"

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

    // UV 変換（スプライトと同方式：拡大・タイル・スクロール対応）
    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);

    // 指定色は sRGB。トーンマップ後の色（リニア）へ直してから掛ける
    float4 materialColor = float4(SRGBToLinear(gMaterial.color.rgb), gMaterial.color.a);

    // テクスチャをサンプリングしてマテリアル色と乗算
    float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);
    output.color = materialColor * textureColor;

    return output;
}
