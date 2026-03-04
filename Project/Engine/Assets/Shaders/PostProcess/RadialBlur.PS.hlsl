#include "FullScreen.hlsli"

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

// ラジアルブラーパラメータ用の定数バッファ
cbuffer RadialBlurParams : register(b0)
{
    float intensity;      // ブラー強度
    float sampleCount;    // サンプル数
    float centerX;        // ブラー中心のX座標
    float centerY;        // ブラー中心のY座標
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
    
    // ブラーの中心点
    float2 center = float2(centerX, centerY);
    
    // 現在のピクセルから中心への方向ベクトル
    float2 dir = input.texcoord - center;
    
    // 中心からの距離
    float distance = length(dir);
    
    // 方向を正規化
    float2 normalizedDir = normalize(dir);
    
    // サンプリング用の変数
    float4 color = float4(0, 0, 0, 0);
    float totalWeight = 0.0f;
    
    // ラジアルブラーのサンプリング
    int samples = max(1, (int)sampleCount);
    
    for (int i = 0; i < samples; i++)
    {
        // サンプリング位置の計算
        float t = (float)i / (float)(samples - 1);
        
        // 中心からの距離に基づいたオフセット
        float offset = intensity * distance * t;
        
        // サンプリング座標
        float2 sampleCoord = input.texcoord - normalizedDir * offset;
        
        // テクスチャの境界チェック
        if (sampleCoord.x >= 0.0f && sampleCoord.x <= 1.0f &&
            sampleCoord.y >= 0.0f && sampleCoord.y <= 1.0f)
        {
            // 重みの計算（中心に近いほど強く）
            float weight = 1.0f - t;
            
            color += gTexture.Sample(gSampler, sampleCoord) * weight;
            totalWeight += weight;
        }
    }
    
    // 重みで正規化
    if (totalWeight > 0.0f)
    {
        color /= totalWeight;
    }
    else
    {
        // フォールバック：元の色
        color = gTexture.Sample(gSampler, input.texcoord);
    }
    
    output.color = color;
    
    return output;
}