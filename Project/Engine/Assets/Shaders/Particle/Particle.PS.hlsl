#include "Particle.hlsli"
#include "FogForward.hlsli"

//SRVのregisterはt0
Texture2D<float32_t4> gTexture : register(t0);
//Samplerのregisterはs0
SamplerState gSampler : register(s0);

// フォグ。C++ 側は「減衰のみ」バリアントを差す（BaseParticleRenderer::SelectFogConstants）。
// 出力がアルファ事前乗算の加算合成なので、内散乱を足すと背後の不透明面に
// 既に乗った分と二重に光る。ここは透過率での減衰だけが正しい。
// 合成なしのパーティクルは全画面の合成の前に描かれるので、C++ 側が「何もしない」バリアントを差す
ConstantBuffer<FogParameters> gFog : register(b0);

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

    // フォグ（不透明・半透明と同じ数式。減衰のみ効く）
    output.color.rgb = ApplyForwardFog(gFog, input.worldPosition, output.color.rgb);

    if (finalColor.a == 0.0f)
    {
        discard; // ピクセルを破棄
    }
    
    return output;
}