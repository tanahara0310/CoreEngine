#include "FullScreen.hlsli"

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

// ブラーパラメータ用の定数バッファ
cbuffer BlurParams : register(b0)
{
    float intensity;      // ブラー強度
    float kernelSize;     // カーネルサイズ
    float2 padding;       // パディング
};

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
    
    // ガウシアンブラーの実装（パラメータ対応版）
    float2 texelSize = 1.0f / float2(1280, 720); // 画面サイズに応じて調整
    texelSize *= kernelSize; // カーネルサイズを適用
    
    float4 color = float4(0, 0, 0, 0);
    float totalWeight = 0.0f;
    
    // 可変サイズのブラーカーネル
    int kernelRadius = max(1, (int)(kernelSize * 2.0f));
    
    for (int x = -kernelRadius; x <= kernelRadius; x++)
    {
        for (int y = -kernelRadius; y <= kernelRadius; y++)
        {
            float2 offset = float2(x, y) * texelSize;
            
            // ガウシアン重みの計算（簡易版）
            float distance = length(float2(x, y));
            float weight = exp(-distance * distance / (2.0f * kernelSize * kernelSize));
            
            color += gTexture.Sample(gSampler, input.texcoord + offset) * weight;
            totalWeight += weight;
        }
    }
    
    // 重みで正規化
    color /= totalWeight;
    
    // 強度を適用（元の色とブラー色をブレンド）
    float4 originalColor = gTexture.Sample(gSampler, input.texcoord);
    output.color = lerp(originalColor, color, intensity);
    
    return output;
}