// SSAO バイラテラルブラー
//
// 入力 SSAO バッファに対して 5x5 のボックスブラーをかけてノイズを除去する。
// ワールド座標の差が大きい箇所はサンプル除外することでエッジを保つ。

#include "FullScreen.hlsli"

Texture2D<float4> gTexture       : register(t0); // 入力 SSAO（PostEffectBase 規約）
Texture2D<float4> gWorldPosition : register(t1); // GBuffer ワールド座標

SamplerState gSampler : register(s0);

cbuffer SSAOBlurParams : register(b0)
{
    float2 gScreenSize;
    float  gDepthThreshold;
    float  _pad0;
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

    int3 centerCoord = int3(input.position.xy, 0);
    float  centerAO  = gTexture.Load(centerCoord).r;
    float4 centerWP  = gWorldPosition.Load(centerCoord);

    if (centerWP.a < 0.5f)
    {
        output.color = float4(centerAO, centerAO, centerAO, 1.0f);
        return output;
    }

    float sum    = 0.0f;
    float weight = 0.0f;

    const int kRadius = 2; // 5x5

    [unroll]
    for (int y = -kRadius; y <= kRadius; ++y)
    {
        [unroll]
        for (int x = -kRadius; x <= kRadius; ++x)
        {
            int2 offset = int2(x, y);
            int3 coord  = int3(centerCoord.xy + offset, 0);

            float4 sampleWP = gWorldPosition.Load(coord);
            if (sampleWP.a < 0.5f) continue;

            float dist = length(sampleWP.xyz - centerWP.xyz);
            if (dist > gDepthThreshold) continue;

            float w = 1.0f;
            sum    += gTexture.Load(coord).r * w;
            weight += w;
        }
    }

    float blurred = (weight > 0.0f) ? (sum / weight) : centerAO;
    output.color = float4(blurred, blurred, blurred, 1.0f);
    return output;
}
