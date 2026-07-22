// SSAO バイラテラルブラー
//
// 入力 SSAO バッファに対して 5x5 のボックスブラーをかけてノイズを除去する。
// ワールド座標の差が大きい箇所はサンプル除外することでエッジを保つ。

#include "FullScreen.hlsli"
#include "../Include/Common/DepthReconstruction.hlsli"

Texture2D<float4> gTexture   : register(t0); // 入力 SSAO（PostEffectBase 規約）
Texture2D<float> gSceneDepth : register(t1); // WorldPosition ターゲット廃止に伴い深度から復元する

SamplerState gSampler : register(s0);

cbuffer SSAOBlurParams : register(b0)
{
    float4x4 gInvViewProj;
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
    float centerAO    = gTexture.Load(centerCoord).r;
    float centerDepth = gSceneDepth.Load(centerCoord);

    if (IsBackgroundDepth(centerDepth))
    {
        output.color = float4(centerAO, centerAO, centerAO, 1.0f);
        return output;
    }

    float2 centerUV = (input.position.xy + 0.5f.xx) / gScreenSize;
    float3 centerWP = ReconstructWorldPosition(ScreenUVToNDC(centerUV), centerDepth, gInvViewProj);

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

            float sampleDepth = gSceneDepth.Load(coord);
            if (IsBackgroundDepth(sampleDepth)) continue;

            float2 sampleUV = (float2(coord.xy) + 0.5f.xx) / gScreenSize;
            float3 sampleWP = ReconstructWorldPosition(ScreenUVToNDC(sampleUV), sampleDepth, gInvViewProj);

            float dist = length(sampleWP - centerWP);
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
