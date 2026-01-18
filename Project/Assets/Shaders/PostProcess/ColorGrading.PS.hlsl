// Color Grading Post Effect Pixel Shader

// 入力テクスチャ
Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

// 定数バッファ
cbuffer ColorGradingParams : register(b0)
{
    float hue;                    // 色相調整
    float saturation;             // 彩度調整
    float value;                  // 明度調整
    float contrast;               // コントラスト
    
    float gamma;                  // ガンマ補正
    float temperature;            // 色温度
    float tint;                   // ティント
    float exposure;               // 露出調整
    
    float3 shadowLift;            // Shadow Lift (RGB)
    float3 midtoneGamma;          // Midtone Gamma (RGB)
    float3 highlightGain;         // Highlight Gain (RGB)
    float padding;
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

// RGB to HSV変換
float3 RGBtoHSV(float3 rgb)
{
    float4 K = float4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    float4 p = lerp(float4(rgb.bg, K.wz), float4(rgb.gb, K.xy), step(rgb.b, rgb.g));
    float4 q = lerp(float4(p.xyw, rgb.r), float4(rgb.r, p.yzx), step(p.x, rgb.r));
    
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return float3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

// HSV to RGB変換
float3 HSVtoRGB(float3 hsv)
{
    float4 K = float4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    float3 p = abs(frac(hsv.xxx + K.xyz) * 6.0 - K.www);
    return hsv.z * lerp(K.xxx, clamp(p - K.xxx, 0.0, 1.0), hsv.y);
}

// 色温度調整関数
float3 ApplyTemperature(float3 color, float temp, float tintValue)
{
    // 色温度調整のための行列
    float3x3 tempMatrix;
    
    if (temp > 0.0) // 暖色
    {
        tempMatrix = float3x3(
            1.0 + temp * 0.3, 0.0, 0.0,
            0.0, 1.0, 0.0,
            0.0, 0.0, 1.0 - temp * 0.2
        );
    }
    else // 寒色
    {
        tempMatrix = float3x3(
            1.0 + temp * 0.2, 0.0, 0.0,
            0.0, 1.0, 0.0,
            0.0, 0.0, 1.0 - temp * 0.3
        );
    }
    
    // ティント調整
    float3x3 tintMatrix = float3x3(
        1.0, 0.0, 0.0,
        tintValue * 0.2, 1.0, 0.0,
        0.0, tintValue * -0.2, 1.0
    );
    
    color = mul(tempMatrix, color);
    color = mul(tintMatrix, color);
    
    return color;
}

// Shadow/Midtone/Highlight調整
float3 ApplySMH(float3 color, float3 shadowLiftRGB, float3 midtoneGammaRGB, float3 highlightGainRGB)
{
    // 輝度計算
    float luminance = dot(color, float3(0.299, 0.587, 0.114));
    
    // Shadow, Midtone, Highlightのウェイト計算
    float shadowWeight = 1.0 - smoothstep(0.0, 0.5, luminance);
    float highlightWeight = smoothstep(0.5, 1.0, luminance);
    float midtoneWeight = 1.0 - shadowWeight - highlightWeight;
    
    // 各調整の適用
    float3 shadowAdjust = color + shadowLiftRGB * shadowWeight;
    float3 midtoneAdjust = pow(abs(shadowAdjust), 1.0 / midtoneGammaRGB) * sign(shadowAdjust);
    float3 highlightAdjust = midtoneAdjust * (highlightGainRGB * highlightWeight + (1.0 - highlightWeight));
    
    return highlightAdjust;
}

PixelShaderOutput main(PixelShaderInput input)
{
    PixelShaderOutput output;
    
    // 入力色の取得
    float3 color = gTexture.Sample(gSampler, input.texcoord).rgb;
    
    // 露出調整
    color *= pow(2.0, exposure);
    
    // 色温度とティント調整
    color = ApplyTemperature(color, temperature, tint);
    
    // HSV調整
    float3 hsv = RGBtoHSV(color);
    hsv.x = frac(hsv.x + hue); // 色相
    hsv.y = saturate(hsv.y * saturation); // 彩度
    hsv.z = hsv.z * value; // 明度
    color = HSVtoRGB(hsv);
    
    // コントラスト調整
    color = (color - 0.5) * contrast + 0.5;
    
    // ガンマ補正
    color = pow(abs(color), 1.0 / gamma) * sign(color);
    
    // Shadow/Midtone/Highlight調整
    color = ApplySMH(color, shadowLift, midtoneGamma, highlightGain);
    
    // 最終的な色の調整
    color = saturate(color);
    
    output.color = float4(color, 1.0);
    
    return output;
}