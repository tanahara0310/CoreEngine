// LensFlareDownsample.CS.hlsl - 輝度抽出 + 1/4 解像度ダウンサンプル
// フル解像度の HDR シーンから閾値を超えた成分だけを 1/4 解像度バッファへ抽出する。
// ここで抽出された明部（太陽・スペキュラハイライト等）がゴースト/ハローの光源になる。

#include "LensFlareCommon.hlsli"

Texture2D<float4> gSceneColor : register(t0);
RWTexture2D<float4> gBright : register(u0);
SamplerState gLinearClamp : register(s0);

static const uint kGroupSize = 8;

[numthreads(kGroupSize, kGroupSize, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    if (dispatchId.x >= gFlareWidth || dispatchId.y >= gFlareHeight)
    {
        return;
    }

    const float2 uv = (float2(dispatchId.xy) + 0.5f) / float2(gFlareWidth, gFlareHeight);
    const float2 fullTexel = 1.0f / float2(gScreenWidth, gScreenHeight);

    // バイリニア 4 タップの箱型ダウンサンプル（1 タップで 2x2、計 4x4 相当）。
    // 単一 Load だと太陽ディスクのような小さな高輝度領域がフレーム毎にちらつく。
    float3 col = 0.0f;
    col += gSceneColor.SampleLevel(gLinearClamp, uv + float2(-1.0f, -1.0f) * fullTexel, 0).rgb;
    col += gSceneColor.SampleLevel(gLinearClamp, uv + float2( 1.0f, -1.0f) * fullTexel, 0).rgb;
    col += gSceneColor.SampleLevel(gLinearClamp, uv + float2(-1.0f,  1.0f) * fullTexel, 0).rgb;
    col += gSceneColor.SampleLevel(gLinearClamp, uv + float2( 1.0f,  1.0f) * fullTexel, 0).rgb;
    col *= 0.25f;

    // ソフトニー付き輝度閾値（Bloom と同形式）
    const float lum = LensFlareLuminance(col);
    const float k = gThreshold * gSoftKnee;
    float soft = lum - gThreshold + k;
    soft = clamp(soft, 0.0f, 2.0f * k);
    soft = soft * soft / (4.0f * k + 0.00001f);
    const float contribution = max(soft, lum - gThreshold) / max(lum, 0.00001f);

    float3 bright = col * contribution;

    // 高輝度クランプ（太陽の数千倍の放射輝度がゴーストで飽和・ちらつきになるのを防ぐ）
    const float brightLum = LensFlareLuminance(bright);
    if (brightLum > gMaxBrightness)
    {
        bright *= gMaxBrightness / brightLum;
    }

    gBright[dispatchId.xy] = float4(bright, 1.0f);
}
