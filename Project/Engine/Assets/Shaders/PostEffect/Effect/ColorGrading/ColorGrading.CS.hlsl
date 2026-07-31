// ColorGrading.CS.hlsl - カラーグレーディング コンピュートシェーダー

#include "ColorSpace.hlsli" // LuminanceRec601

Texture2D<float4> gTexture : register(t0);
RWTexture2D<float4> gOutput : register(u0);

cbuffer ColorGradingParams : register(b0)
{
    float hue;          // 色相調整
    float saturation;   // 彩度調整
    float value;        // 明度調整
    float contrast;     // コントラスト

    float gamma;        // ガンマ補正
    float temperature;  // 色温度
    float tint;         // ティント
    float exposure;     // 露出調整

    float3 shadowLift;     // Shadow Lift (RGB)
    float3 midtoneGamma;   // Midtone Gamma (RGB)
    float3 highlightGain;  // Highlight Gain (RGB)
    float padding;
};

cbuffer ScreenParams : register(b1)
{
    uint screenWidth;
    uint screenHeight;
    float2 pad;
};

static const uint kGroupSize = 8;

// RGB to HSV 変換
float3 RGBtoHSV(float3 rgb)
{
    float4 K = float4(0.0f, -1.0f / 3.0f, 2.0f / 3.0f, -1.0f);
    float4 p = lerp(float4(rgb.bg, K.wz), float4(rgb.gb, K.xy), step(rgb.b, rgb.g));
    float4 q = lerp(float4(p.xyw, rgb.r), float4(rgb.r, p.yzx), step(p.x, rgb.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10f;
    return float3(abs(q.z + (q.w - q.y) / (6.0f * d + e)), d / (q.x + e), q.x);
}

// HSV to RGB 変換
float3 HSVtoRGB(float3 hsv)
{
    float4 K = float4(1.0f, 2.0f / 3.0f, 1.0f / 3.0f, 3.0f);
    float3 p = abs(frac(hsv.xxx + K.xyz) * 6.0f - K.www);
    return hsv.z * lerp(K.xxx, clamp(p - K.xxx, 0.0f, 1.0f), hsv.y);
}

// 色温度調整
float3 ApplyTemperature(float3 col, float temp, float tintValue)
{
    float3x3 tempMatrix;
    if (temp > 0.0f)
    {
        tempMatrix = float3x3(
            1.0f + temp * 0.3f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 1.0f - temp * 0.2f
        );
    }
    else
    {
        tempMatrix = float3x3(
            1.0f + temp * 0.2f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 1.0f - temp * 0.3f
        );
    }
    float3x3 tintMatrix = float3x3(
        1.0f, 0.0f, 0.0f,
        tintValue * 0.2f, 1.0f, 0.0f,
        0.0f, tintValue * -0.2f, 1.0f
    );
    col = mul(tempMatrix, col);
    col = mul(tintMatrix, col);
    return col;
}

// Shadow/Midtone/Highlight 調整
float3 ApplySMH(float3 col, float3 shadowLiftRGB, float3 midtoneGammaRGB, float3 highlightGainRGB)
{
    float lum = LuminanceRec601(col);
    float shadowW = 1.0f - smoothstep(0.0f, 0.5f, lum);
    float highlightW = smoothstep(0.5f, 1.0f, lum);
    float3 shadowAdj = col + shadowLiftRGB * shadowW;
    float3 midtoneAdj = pow(abs(shadowAdj), 1.0f / midtoneGammaRGB) * sign(shadowAdj);
    float3 highlightAdj = midtoneAdj * (highlightGainRGB * highlightW + (1.0f - highlightW));
    return highlightAdj;
}

[numthreads(kGroupSize, kGroupSize, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    if (dispatchId.x >= screenWidth || dispatchId.y >= screenHeight)
    {
        return;
    }

    float3 col = gTexture.Load(int3(dispatchId.xy, 0)).rgb;

    // 露出調整
    col *= pow(2.0f, exposure);

    // 色温度・ティント
    col = ApplyTemperature(col, temperature, tint);

    // HSV 調整
    float3 hsv = RGBtoHSV(col);
    hsv.x = frac(hsv.x + hue);
    hsv.y = saturate(hsv.y * saturation);
    hsv.z = hsv.z * value;
    col = HSVtoRGB(hsv);

    // コントラスト
    col = (col - 0.5f) * contrast + 0.5f;

    // ガンマ補正
    col = pow(abs(col), 1.0f / gamma) * sign(col);

    // SMH 調整
    col = ApplySMH(col, shadowLift, midtoneGamma, highlightGain);
    col = saturate(col);

    gOutput[dispatchId.xy] = float4(col, 1.0f);
}
