// テクスチャとサンプラー
Texture2D gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

struct PSOutput
{
    float4 color : SV_TARGET0;
};

PSOutput main(PSInput input)
{
    PSOutput output;

    // 入力テクスチャカラー取得
    output.color = gTexture.Sample(gSampler, input.texcoord);

    // 色を反転（ネガポジ効果）
    // RGB各成分を 1.0 から減算することで反転
    output.color.rgb = 1.0f - output.color.rgb;
    
    // アルファ値はそのまま保持
    // output.color.a はそのまま

    return output;
}