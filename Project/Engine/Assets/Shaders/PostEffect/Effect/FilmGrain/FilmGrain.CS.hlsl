// FilmGrain.CS.hlsl - フィルムグレイン

#include "ColorSpace.hlsli" // LuminanceRec601
#include "Hash.hlsli"       // Hash12 / Hash32

Texture2D<float4>   gTexture : register(t0);
RWTexture2D<float4> gOutput  : register(u0);

cbuffer FilmGrainParams : register(b0)
{
    float intensity;           // 全体強度
    float intensityShadows;    // シャドウ帯の倍率
    float intensityMidtones;   // 中間調帯の倍率
    float intensityHighlights; // ハイライト帯の倍率

    float grainSize;           // 粒の大きさ[px]。1 で 1 ピクセル、2 で 2x2 相当
    float chromaAmount;        // 色ノイズの割合。0 でモノクロ粒
    float time;                // 実行時の経過時間（粒を毎フレーム散らす）
    float padding;
};

cbuffer ScreenParams : register(b1)
{
    uint screenWidth;
    uint screenHeight;
    float2 pad;
};

static const uint kGroupSize = 8;

[numthreads(kGroupSize, kGroupSize, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    if (dispatchId.x >= screenWidth || dispatchId.y >= screenHeight)
    {
        return;
    }

    float4 baseColor = gTexture.Load(int3(dispatchId.xy, 0));

    // 粒の座標。grainSize で割ることで粒を大きくする（値を共有する範囲が広がる）
    float2 grainCoord = floor((float2)dispatchId.xy / max(grainSize, 1.0f));
    // 時刻をずらして毎フレーム違う粒にする。整数へ丸めてフレーム内では固定
    float2 noiseCoord = grainCoord + floor(time * 60.0f) * float2(17.0f, 29.0f);

    float  monoNoise  = Hash12(noiseCoord) * 2.0f - 1.0f;
    float3 colorNoise = Hash32(noiseCoord + 13.0f) * 2.0f - 1.0f;

    // 輝度で 3 帯の重みを作る。PostTonemap 段なので色は [0,1] に収まっており、
    // 0.5 を境にした smoothstep がそのまま使える
    float lum = LuminanceRec601(baseColor.rgb);
    float shadowWeight    = 1.0f - smoothstep(0.0f, 0.5f, lum);
    float highlightWeight = smoothstep(0.5f, 1.0f, lum);
    float midtoneWeight   = max(0.0f, 1.0f - shadowWeight - highlightWeight);

    float bandIntensity =
          shadowWeight    * intensityShadows
        + midtoneWeight   * intensityMidtones
        + highlightWeight * intensityHighlights;

    float amount = intensity * bandIntensity;

    // モノクロ粒を主体にし、色ノイズは補助的に混ぜる（色が強いとデジタルノイズに見える）
    float3 grain = monoNoise.xxx * (1.0f - chromaAmount) + colorNoise * chromaAmount;

    gOutput[dispatchId.xy] = float4(saturate(baseColor.rgb + grain * amount), baseColor.a);
}
