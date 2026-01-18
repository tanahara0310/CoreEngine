#include "FullScreen.hlsli"

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

// ブルームパラメータ用の定数バッファ
cbuffer BloomParams : register(b0)
{
    float threshold;        // 輝度閾値
    float intensity;        // ブルーム強度
    float blurRadius;       // ブラー半径
    float softKnee;         // ソフトニー
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

// 輝度計算（Rec.709）
float Luminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

// ソフトスレッショルド関数
float SoftThreshold(float lum, float threshold, float softKnee)
{
    float knee = threshold * softKnee;
    float soft = lum - threshold + knee;
    soft = clamp(soft, 0.0f, 2.0f * knee);
    soft = soft * soft / (4.0f * knee + 0.00001f);
    
    float contribution = max(soft, lum - threshold);
    contribution /= max(lum, 0.00001f);
    
    return contribution;
}

// ガウシアンブラーのサンプリング
float4 GaussianBlur(float2 texcoord, float2 texelSize)
{
    float4 color = float4(0, 0, 0, 0);
    float totalWeight = 0.0f;
    
    // ブラー半径に基づいてカーネルサイズを決定
    int kernelRadius = max(1, (int)(blurRadius * 2.0f));
    
    for (int x = -kernelRadius; x <= kernelRadius; x++)
    {
        for (int y = -kernelRadius; y <= kernelRadius; y++)
        {
            float2 offset = float2(x, y) * texelSize * blurRadius;
            
            // ガウシアン重みの計算
            float distance = length(float2(x, y));
            float weight = exp(-distance * distance / (2.0f * blurRadius * blurRadius));
            
            color += gTexture.Sample(gSampler, texcoord + offset) * weight;
            totalWeight += weight;
        }
    }
    
    return color / totalWeight;
}

PixelShaderOutput main(PixelShaderInput input)
{
    PixelShaderOutput output;
    
    // テクセルサイズの計算
    float2 texelSize = 1.0f / float2(1280, 720); // 画面サイズに応じて調整
    
    // 元の色を取得
    float4 originalColor = gTexture.Sample(gSampler, input.texcoord);
    
    // ガウシアンブラーを適用
    float4 blurredColor = GaussianBlur(input.texcoord, texelSize);
    
    // 輝度を計算
    float lum = Luminance(blurredColor.rgb);
    
    // ソフトスレッショルドで輝度抽出
    float bloomContribution = SoftThreshold(lum, threshold, softKnee);
    
    // ブルームカラーを計算
    float3 bloomColor = blurredColor.rgb * bloomContribution;
    
    // 元の画像とブルームを合成
    float3 finalColor = originalColor.rgb + bloomColor * intensity;
    
    output.color = float4(finalColor, originalColor.a);
    
    return output;
}
