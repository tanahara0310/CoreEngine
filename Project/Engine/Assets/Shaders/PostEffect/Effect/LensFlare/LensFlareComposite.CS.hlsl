// LensFlareComposite.CS.hlsl - レンズフレア合成
// ブラー済みフレアバッファをスターバースト（放射状ストリーク）で変調し、
// HDR シーンへ加算合成する。トーンマッピングの前段で実行されることを前提とする。

#include "LensFlareCommon.hlsli"

Texture2D<float4> gTexture : register(t0); // フル解像度の HDR 入力
Texture2D<float4> gFlare : register(t1);   // 1/4 解像度のフレア（ブラー済み）
RWTexture2D<float4> gOutput : register(u0);
SamplerState gLinearClamp : register(s0);

static const uint kGroupSize = 8;

// 角度方向の 1 次元値ノイズ。格子数を整数にして 2π 周期で連続にする。
float AngularNoise(float ang, float cells)
{
    const float x = (ang / TWO_PI + 0.5f) * cells;
    const float i = floor(x);
    float f = frac(x);
    f = f * f * (3.0f - 2.0f * f);
    const float a = HashSine11(fmod(i, cells));
    const float b = HashSine11(fmod(i + 1.0f, cells));
    return lerp(a, b, f);
}

[numthreads(kGroupSize, kGroupSize, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    if (dispatchId.x >= gScreenWidth || dispatchId.y >= gScreenHeight)
    {
        return;
    }

    const float4 scene = gTexture.Load(int3(dispatchId.xy, 0));
    const float2 uv = (float2(dispatchId.xy) + 0.5f) / float2(gScreenWidth, gScreenHeight);

    const float3 flare = gFlare.SampleLevel(gLinearClamp, uv, 0).rgb;

    // ===== スターバースト変調 =====
    // 画面中心からの角度に沿った不規則な放射状ストリーク。回折格子のように
    // フレアへ細かい明暗の筋を乗せる（UE の Starburst テクスチャ相当を手続き生成）。
    const float aspect = (float)gScreenWidth / (float)gScreenHeight;
    float2 d = uv - 0.5f;
    d.x *= aspect;
    const float ang = atan2(d.y, d.x);

    float streak = 0.65f * AngularNoise(ang, 128.0f)
                 + 0.35f * AngularNoise(ang + 1.234f, 48.0f);
    streak = 0.35f + 1.3f * streak;

    // 画面中心付近はストリークを弱め、外周ほど筋が立つようにする
    const float radialFade = saturate(length(d) * 2.0f);
    const float modulator = lerp(1.0f, streak, gStarburstIntensity * radialFade);

    const float3 finalColor = scene.rgb + flare * gTint.rgb * gIntensity * modulator;
    gOutput[dispatchId.xy] = float4(finalColor, scene.a);
}
