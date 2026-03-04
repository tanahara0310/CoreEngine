#include "Particle.hlsli"

//SRVのregisterはt0
Texture2D<float32_t4> gTexture : register(t0);
//Samplerのregisterはs0
SamplerState gSampler : register(s0);

struct PixelShaderOutput
{
    float32_t4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    
    //テクスチャをサンプリング
    float32_t4 textureColor = gTexture.Sample(gSampler, input.texcoord);
    
    // パーティクルの色とテクスチャ色を乗算
    float32_t4 finalColor = textureColor * input.color;
    
    // 加算ブレンド用：RGB値にアルファを事前乗算
    output.color.rgb = finalColor.rgb * finalColor.a;
    output.color.a = 1.0f;
    
    if (finalColor.a == 0.0f)
    {
        discard; // ピクセルを破棄
    }
    
    return output;
}