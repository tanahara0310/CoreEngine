#include "PostEffectMaterial.hlsli"

// ブラウン管の走査線（横縞を流す）・ちらつき・周辺減光
struct Params
{
    float lineInterval;     // 走査線の間隔（px）
    float lineStrength;     // 暗くする強さ
    float scrollSpeed;      // 流れる速さ
    float flickerStrength;  // ちらつきの強さ
    float flickerSpeed;     // ちらつきの速さ
    float vignetteStrength; // 周辺減光の強さ
    float lineWidth;        // 暗い帯の太さの割合
};
ConstantBuffer<Params> gMaterial : register(b0);

float Hash(float n)
{
    return frac(sin(n) * 43758.5453f);
}

float Vignette(float2 uv, float strength)
{
    float2 c = uv - 0.5f;
    float dist = dot(c, c) * 4.0f;
    return 1.0f - saturate(dist * strength);
}

POST_EFFECT_THREADS
void main(uint3 id : SV_DispatchThreadID)
{
    if (IsOutsideScreen(id.xy)) {
        return;
    }

    float2 uv = PostEffectUV(id.xy);
    float4 original = SampleScreen(uv);
    float time = gPostEffect.time;

    float screenHeight = float(gPostEffect.screenSize.y);
    float pixelY    = uv.y * screenHeight;
    float scrolledY = pixelY + time * gMaterial.scrollSpeed * screenHeight * 0.1f;
    float linePhase = fmod(scrolledY, gMaterial.lineInterval) / gMaterial.lineInterval;

    float darkBand   = 1.0f - step(gMaterial.lineWidth, linePhase);
    float scanDarken = 1.0f - darkBand * gMaterial.lineStrength;

    float flicker = 1.0f;
    if (gMaterial.flickerStrength > 0.0f)
    {
        float ft = floor(time * gMaterial.flickerSpeed);
        flicker  = 1.0f - gMaterial.flickerStrength * (Hash(ft) * 0.5f);
    }

    float vig = Vignette(uv, gMaterial.vignetteStrength);

    gOutput[id.xy] = float4(original.rgb * scanDarken * flicker * vig, 1.0f);
}
