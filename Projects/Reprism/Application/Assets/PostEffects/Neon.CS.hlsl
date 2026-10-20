#include "PostEffectMaterial.hlsli"

// 明るい所を彩度を上げてにじませ、輪郭を光らせるネオン調の仕上げ（トーンマップの前）
struct Params
{
    float  bloomThreshold; // にじませる明るさのしきい値
    float  bloomIntensity; // にじみの強さ
    float  bloomRadius;    // にじみの広がり（px）
    float  saturation;     // 彩度の倍率
    float  edgeIntensity;  // 輪郭の光の強さ
    float  colorShift;     // 色相をずらす量
    float  darknessCrush;  // 暗い所を沈める強さ
};
ConstantBuffer<Params> gMaterial : register(b0);

float3 RGBtoHSV(float3 c)
{
    float4 K = float4(0.0f, -1.0f / 3.0f, 2.0f / 3.0f, -1.0f);
    float4 p = (c.g < c.b) ? float4(c.bg, K.wz) : float4(c.gb, K.xy);
    float4 q = (c.r < p.x) ? float4(p.xyw, c.r) : float4(c.r, p.yzx);
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10f;
    return float3(abs(q.z + (q.w - q.y) / (6.0f * d + e)), d / (q.x + e), q.x);
}

float3 HSVtoRGB(float3 c)
{
    float4 K = float4(1.0f, 2.0f / 3.0f, 1.0f / 3.0f, 3.0f);
    float3 p = abs(frac(c.xxx + K.xyz) * 6.0f - K.www);
    return c.z * lerp(K.xxx, clamp(p - K.xxx, 0.0f, 1.0f), c.y);
}

float3 EnhanceSaturation(float3 color, float sat)
{
    float3 hsv = RGBtoHSV(color);
    hsv.y = saturate(hsv.y * sat);
    return HSVtoRGB(hsv);
}

// しきい値より明るい色だけを、9×9 のガウス重みで集める
float3 GlowSample(float2 uv, float2 texelSize, float radius)
{
    float3 result = float3(0.0f, 0.0f, 0.0f);
    float totalWeight = 0.0f;
    float stepSize = radius / 4.0f;
    [unroll]
    for (int x = -4; x <= 4; x++)
    {
        [unroll]
        for (int y = -4; y <= 4; y++)
        {
            float2 offset = float2(x, y) * texelSize * stepSize;
            float dist2 = float(x * x + y * y);
            float weight = exp(-dist2 / 8.0f);
            float3 s = SampleScreen(uv + offset).rgb;
            float lum = Luminance(s);
            float mask = saturate((lum - gMaterial.bloomThreshold) / max(0.3f, 1.0f - gMaterial.bloomThreshold));
            result += s * weight * mask;
            totalWeight += weight;
        }
    }
    return result / max(totalWeight, 0.0001f);
}

// ソーベルで輪郭の強さを出す
float DetectEdge(float2 uv, float2 texelSize)
{
    float3 s00 = SampleScreen(uv + float2(-1, -1) * texelSize).rgb;
    float3 s10 = SampleScreen(uv + float2( 0, -1) * texelSize).rgb;
    float3 s20 = SampleScreen(uv + float2( 1, -1) * texelSize).rgb;
    float3 s01 = SampleScreen(uv + float2(-1,  0) * texelSize).rgb;
    float3 s21 = SampleScreen(uv + float2( 1,  0) * texelSize).rgb;
    float3 s02 = SampleScreen(uv + float2(-1,  1) * texelSize).rgb;
    float3 s12 = SampleScreen(uv + float2( 0,  1) * texelSize).rgb;
    float3 s22 = SampleScreen(uv + float2( 1,  1) * texelSize).rgb;

    float3 gx = -s00 - 2.0f * s01 - s02 + s20 + 2.0f * s21 + s22;
    float3 gy = -s00 - 2.0f * s10 - s20 + s02 + 2.0f * s12 + s22;
    return saturate(length(float2(length(gx), length(gy))) * 0.4f);
}

POST_EFFECT_THREADS
void main(uint3 id : SV_DispatchThreadID)
{
    if (IsOutsideScreen(id.xy)) {
        return;
    }

    float2 uv = PostEffectUV(id.xy);
    float2 texelSize = gPostEffect.invScreenSize;
    float4 original = SampleScreen(uv);

    float3 saturated = EnhanceSaturation(original.rgb, gMaterial.saturation);

    float3 hsv = RGBtoHSV(saturated);
    hsv.x = frac(hsv.x + gMaterial.colorShift);
    float3 shifted = HSVtoRGB(hsv);

    float3 glow1 = GlowSample(uv, texelSize, gMaterial.bloomRadius * 0.5f);
    float3 glow2 = GlowSample(uv, texelSize, gMaterial.bloomRadius * 1.0f);
    float3 glow3 = GlowSample(uv, texelSize, gMaterial.bloomRadius * 2.2f);

    float3 glowColor =
        EnhanceSaturation(glow1, gMaterial.saturation * 1.8f) * 1.6f +
        EnhanceSaturation(glow2, gMaterial.saturation * 1.5f) * 1.0f +
        EnhanceSaturation(glow3, gMaterial.saturation * 1.2f) * 0.5f;

    float edge = DetectEdge(uv, texelSize);
    float edgeLumGate = saturate((Luminance(original.rgb) - gMaterial.bloomThreshold) / max(gMaterial.bloomThreshold, 0.001f));
    float3 edgeColor = EnhanceSaturation(original.rgb, gMaterial.saturation * 2.2f) * edge * gMaterial.edgeIntensity * edgeLumGate;

    float brightness = Luminance(shifted);
    float crushFactor = saturate(brightness / max(gMaterial.bloomThreshold, 0.001f));
    crushFactor = lerp(1.0f, crushFactor, gMaterial.darknessCrush);
    float3 crushed = shifted * crushFactor;

    float3 finalColor = crushed + glowColor * gMaterial.bloomIntensity + edgeColor;
    // 不透明（a = 1）で出す
    gOutput[id.xy] = float4(finalColor, 1.0f);
}
