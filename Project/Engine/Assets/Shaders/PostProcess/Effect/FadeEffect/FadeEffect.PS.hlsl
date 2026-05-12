// Enhanced Fade Effect Post Process Pixel Shader

// 入力テクスチャ
Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

// 定数バッファ
cbuffer FadeParams : register(b0)
{
    float fadeAlpha;        // フェード強度 (0.0 = 透明, 1.0 = 完全フェード)
    float fadeType;         // フェードタイプ (0:Black, 1:White, 2:Spiral, 3:Ripple, 4:Glitch, 5:Portal)
    float time;             // 時間パラメータ
    float spiralPower;      // 渦巻きの強さ
    float rippleFreq;       // 波紋の周波数
    float glitchIntensity;  // グリッチの強さ
    float portalSize;       // ポータルサイズ
    float colorShift;       // 色相シフト
    float2 padding;         // パディング
};

// 入力構造体
struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

// 出力構造体
struct PixelShaderOutput
{
    float4 color : SV_Target;
};

// HSVからRGBへの変換
float3 HSVtoRGB(float3 hsv)
{
    float4 k = float4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    float3 p = abs(frac(hsv.xxx + k.xyz) * 6.0 - k.www);
    return hsv.z * lerp(k.xxx, saturate(p - k.xxx), hsv.y);
}

// RGB to HSV変換
float3 RGBtoHSV(float3 rgb)
{
    float4 k = float4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    float4 p = lerp(float4(rgb.bg, k.wz), float4(rgb.gb, k.xy), step(rgb.b, rgb.g));
    float4 q = lerp(float4(p.xyw, rgb.r), float4(rgb.r, p.yzx), step(p.x, rgb.r));
    
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return float3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

// ランダム関数
float random(float2 st)
{
    return frac(sin(dot(st.xy, float2(12.9898, 78.233))) * 43758.5453123);
}

// ノイズ関数
float noise(float2 st)
{
    float2 i = floor(st);
    float2 f = frac(st);
    
    float a = random(i);
    float b = random(i + float2(1.0, 0.0));
    float c = random(i + float2(0.0, 1.0));
    float d = random(i + float2(1.0, 1.0));
    
    float2 u = f * f * (3.0 - 2.0 * f);
    
    return lerp(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}

// 渦巻きフェード
float SpiralFade(float2 uv, float alpha, float power, float t)
{
    float2 center = float2(0.5, 0.5);
    float2 toCenter = uv - center;
    float dist = length(toCenter);
    float angle = atan2(toCenter.y, toCenter.x);
    
    // 渦巻きの計算
    float spiral = angle + dist * power + t * 2.0;
    float spiralMask = sin(spiral * 3.14159) * 0.5 + 0.5;
    
    // 距離に基づくフェード
    float distanceFade = 1.0 - smoothstep(0.0, 0.7, dist);
    
    return saturate(spiralMask * alpha + (1.0 - distanceFade) * alpha);
}

// 波紋フェード
float RippleFade(float2 uv, float alpha, float freq, float t)
{
    float2 center = float2(0.5, 0.5);
    float dist = length(uv - center);
    
    // 波紋の計算
    float ripple = sin(dist * freq - t * 5.0) * 0.5 + 0.5;
    float rippleMask = smoothstep(0.0, 1.0, ripple);
    
    // 中心からの距離に基づくフェード
    float distanceFade = smoothstep(0.0, 1.0, dist);
    
    return saturate(rippleMask * alpha + distanceFade * alpha);
}

// グリッチフェード
float GlitchFade(float2 uv, float alpha, float intensity, float t)
{
    // デジタルノイズ
    float2 noiseUV = uv * 50.0 + t * 10.0;
    float digitalNoise = step(0.5, noise(noiseUV));
    
    // 水平ライン（変数名をlineからhorizontalLineに変更）
    float horizontalLine = step(0.99, sin(uv.y * 100.0 + t * 20.0));
    
    // グリッチマスク
    float glitchMask = saturate(digitalNoise * intensity + horizontalLine * intensity);
    
    return saturate(glitchMask * alpha + alpha * 0.3);
}

// ポータルフェード
float PortalFade(float2 uv, float alpha, float size, float t)
{
    float2 center = float2(0.5, 0.5);
    float dist = length(uv - center);
    
    // ポータルの輪郭
    float portal = smoothstep(size - 0.1, size, dist) - smoothstep(size, size + 0.1, dist);
    
    // 回転効果
    float angle = atan2(uv.y - 0.5, uv.x - 0.5) + t * 2.0;
    float rotation = sin(angle * 4.0) * 0.5 + 0.5;
    
    // エネルギー効果
    float energy = sin(dist * 10.0 - t * 8.0) * 0.5 + 0.5;
    
    float portalMask = portal * rotation * energy;
    float centerFade = 1.0 - smoothstep(0.0, size, dist);
    
    return saturate(portalMask * alpha + centerFade * alpha);
}

PixelShaderOutput main(PixelShaderInput input)
{
    PixelShaderOutput output;
    
    // 入力色の取得
    float3 originalColor = gTexture.Sample(gSampler, input.texcoord).rgb;
    float2 uv = input.texcoord;
    
    float finalAlpha = fadeAlpha;
    float3 fadeColor = float3(0.0, 0.0, 0.0); // デフォルトは黒
    
    // フェードタイプに応じた処理
    if (fadeType < 0.5) {
        // 黒フェード
        fadeColor = float3(0.0, 0.0, 0.0);
    }
    else if (fadeType < 1.5) {
        // 白フェード
        fadeColor = float3(1.0, 1.0, 1.0);
    }
    else if (fadeType < 2.5) {
        // 渦巻きフェード
        finalAlpha = SpiralFade(uv, fadeAlpha, spiralPower, time);
        
        // 色相シフトを適用
        float3 hsv = RGBtoHSV(originalColor);
        hsv.x += colorShift;
        originalColor = HSVtoRGB(hsv);
        
        fadeColor = float3(0.2, 0.1, 0.4); // 紫っぽい色
    }
    else if (fadeType < 3.5) {
        // 波紋フェード
        finalAlpha = RippleFade(uv, fadeAlpha, rippleFreq, time);
        
        // 青っぽい神秘的な色
        fadeColor = float3(0.1, 0.3, 0.6);
    }
    else if (fadeType < 4.5) {
        // グリッチフェード
        finalAlpha = GlitchFade(uv, fadeAlpha, glitchIntensity, time);
        
        // 色収差効果
        float2 offset = float2(0.005, 0.0) * glitchIntensity;
        float r = gTexture.Sample(gSampler, uv + offset).r;
        float g = originalColor.g;
        float b = gTexture.Sample(gSampler, uv - offset).b;
        originalColor = float3(r, g, b);
        
        fadeColor = float3(1.0, 0.0, 0.5); // マゼンタ
    }
    else {
        // ポータルフェード
        finalAlpha = PortalFade(uv, fadeAlpha, portalSize, time);
        
        // エネルギー的な色
        fadeColor = float3(0.0, 1.0, 0.8);
    }
    
    // フェード強度に基づいて色を補間
    float3 finalColor = lerp(originalColor, fadeColor, finalAlpha);
    
    output.color = float4(finalColor, 1.0);
    
    return output;
}