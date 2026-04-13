// ToneMapping Post Effect Pixel Shader
// HDR→LDR変換をポストエフェクトチェーンの最終段で適用する

#include "FullScreen.hlsli"

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

// ACES トーンマッピング（映画業界標準）
float3 ACESFilm(float3 x)
{
    const float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

PixelShaderOutput main(PixelShaderInput input)
{
    PixelShaderOutput output;

    float3 hdrColor = gTexture.Sample(gSampler, input.texcoord).rgb;

    // ACES トーンマッピング（HDR→LDR）
    output.color = float4(ACESFilm(hdrColor), 1.0f);

    return output;
}
