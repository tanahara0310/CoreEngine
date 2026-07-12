// FadeEffect.CS.hlsl - フェード効果 コンピュートシェーダー

Texture2D<float4> gTexture : register(t0);
RWTexture2D<float4> gOutput : register(u0);

cbuffer FadeParams : register(b0)
{
    float fadeAlpha;       // フェード強度 (0.0 = 透明, 1.0 = 完全フェード)
    float fadeType;        // フェードタイプ (0:Black, 1:White, 2:Spiral, 3:Ripple, 4:Glitch, 5:Portal)
    float time;            // 時間パラメータ
    float spiralPower;     // 螺旋の強さ
    float rippleFreq;      // 波紋の周波数
    float glitchIntensity; // グリッチの強さ
    float portalSize;      // ポータルサイズ
    float colorShift;      // 色相シフト
    float2 padding;
};

cbuffer ScreenParams : register(b1)
{
    uint screenWidth;
    uint screenHeight;
    float2 pad;
};

static const uint kGroupSize = 8;

// HSV to RGB
float3 HSVtoRGB(float3 hsv)
{
    float4 k = float4(1.0f, 2.0f / 3.0f, 1.0f / 3.0f, 3.0f);
    float3 p = abs(frac(hsv.xxx + k.xyz) * 6.0f - k.www);
    return hsv.z * lerp(k.xxx, saturate(p - k.xxx), hsv.y);
}

// RGB to HSV
float3 RGBtoHSV(float3 rgb)
{
    float4 k = float4(0.0f, -1.0f / 3.0f, 2.0f / 3.0f, -1.0f);
    float4 p = lerp(float4(rgb.bg, k.wz), float4(rgb.gb, k.xy), step(rgb.b, rgb.g));
    float4 q = lerp(float4(p.xyw, rgb.r), float4(rgb.r, p.yzx), step(p.x, rgb.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10f;
    return float3(abs(q.z + (q.w - q.y) / (6.0f * d + e)), d / (q.x + e), q.x);
}

// 乱数
float RandomVal(float2 st)
{
    return frac(sin(dot(st.xy, float2(12.9898f, 78.233f))) * 43758.5453123f);
}

// ノイズ
float NoiseVal(float2 st)
{
    float2 i = floor(st);
    float2 f = frac(st);
    float a = RandomVal(i);
    float b = RandomVal(i + float2(1.0f, 0.0f));
    float c = RandomVal(i + float2(0.0f, 1.0f));
    float d = RandomVal(i + float2(1.0f, 1.0f));
    float2 u = f * f * (3.0f - 2.0f * f);
    return lerp(a, b, u.x) + (c - a) * u.y * (1.0f - u.x) + (d - b) * u.x * u.y;
}

// 螺旋フェード
float SpiralFade(float2 uv, float alpha, float power, float t)
{
    float2 toCenter = uv - float2(0.5f, 0.5f);
    float dist = length(toCenter);
    float angle = atan2(toCenter.y, toCenter.x);
    float spiral = angle + dist * power + t * 2.0f;
    float spiralMask = sin(spiral * 3.14159f) * 0.5f + 0.5f;
    float distanceFade = 1.0f - smoothstep(0.0f, 0.7f, dist);
    return saturate(spiralMask * alpha + (1.0f - distanceFade) * alpha);
}

// 波紋フェード
float RippleFade(float2 uv, float alpha, float freq, float t)
{
    float dist = length(uv - float2(0.5f, 0.5f));
    float ripple = sin(dist * freq - t * 5.0f) * 0.5f + 0.5f;
    float rippleMask = smoothstep(0.0f, 1.0f, ripple);
    float distanceFade = smoothstep(0.0f, 1.0f, dist);
    return saturate(rippleMask * alpha + distanceFade * alpha);
}

// グリッチフェード
float GlitchFade(float2 uv, float alpha, float intensity, float t)
{
    float2 noiseUV = uv * 50.0f + t * 10.0f;
    float digitalNoise = step(0.5f, NoiseVal(noiseUV));
    float horizontalLine = step(0.99f, sin(uv.y * 100.0f + t * 20.0f));
    float glitchMask = saturate(digitalNoise * intensity + horizontalLine * intensity);
    return saturate(glitchMask * alpha + alpha * 0.3f);
}

// ポータルフェード
float PortalFade(float2 uv, float alpha, float size, float t)
{
    float dist = length(uv - float2(0.5f, 0.5f));
    float portal = smoothstep(size - 0.1f, size, dist) - smoothstep(size, size + 0.1f, dist);
    float angle = atan2(uv.y - 0.5f, uv.x - 0.5f) + t * 2.0f;
    float rotation = sin(angle * 4.0f) * 0.5f + 0.5f;
    float energy = sin(dist * 10.0f - t * 8.0f) * 0.5f + 0.5f;
    float portalMask = portal * rotation * energy;
    float centerFade = 1.0f - smoothstep(0.0f, size, dist);
    return saturate(portalMask * alpha + centerFade * alpha);
}

[numthreads(kGroupSize, kGroupSize, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    if (dispatchId.x >= screenWidth || dispatchId.y >= screenHeight)
    {
        return;
    }

    float2 uv = float2(
        (float)dispatchId.x / (float)screenWidth,
        (float)dispatchId.y / (float)screenHeight
    );

    float3 originalColor = gTexture.Load(int3(dispatchId.xy, 0)).rgb;
    float finalAlpha = fadeAlpha;
    float3 fadeColor = float3(0.0f, 0.0f, 0.0f);

    if (fadeType < 0.5f)
    {
        // ブラックフェード
        fadeColor = float3(0.0f, 0.0f, 0.0f);
    }
    else if (fadeType < 1.5f)
    {
        // ホワイトフェード
        fadeColor = float3(1.0f, 1.0f, 1.0f);
    }
    else if (fadeType < 2.5f)
    {
        // 螺旋フェード
        finalAlpha = SpiralFade(uv, fadeAlpha, spiralPower, time);
        float3 hsv = RGBtoHSV(originalColor);
        hsv.x += colorShift;
        originalColor = HSVtoRGB(hsv);
        fadeColor = float3(0.2f, 0.1f, 0.4f);
    }
    else if (fadeType < 3.5f)
    {
        // 波紋フェード
        finalAlpha = RippleFade(uv, fadeAlpha, rippleFreq, time);
        fadeColor = float3(0.1f, 0.3f, 0.6f);
    }
    else if (fadeType < 4.5f)
    {
        // グリッチフェード
        finalAlpha = GlitchFade(uv, fadeAlpha, glitchIntensity, time);
        // 色収差オフセット（テクセル座標で近似）
        int2 offset = int2((int)(0.005f * screenWidth * glitchIntensity), 0);
        int2 redCoord  = clamp((int2)dispatchId.xy + offset, int2(0,0), int2(screenWidth-1, screenHeight-1));
        int2 blueCoord = clamp((int2)dispatchId.xy - offset, int2(0,0), int2(screenWidth-1, screenHeight-1));
        float r = gTexture.Load(int3(redCoord,       0)).r;
        float b = gTexture.Load(int3(blueCoord,      0)).b;
        originalColor = float3(r, originalColor.g, b);
        fadeColor = float3(1.0f, 0.0f, 0.5f);
    }
    else
    {
        // ポータルフェード
        finalAlpha = PortalFade(uv, fadeAlpha, portalSize, time);
        fadeColor = float3(0.0f, 1.0f, 0.8f);
    }

    float3 finalColor = lerp(originalColor, fadeColor, finalAlpha);
    gOutput[dispatchId.xy] = float4(finalColor, 1.0f);
}
