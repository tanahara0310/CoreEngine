// ============================================================
// 流体描画：しぶきの深度をならす（縦横を同時に見るバイラテラルフィルタ）
// ------------------------------------------------------------
// ならす幅は世界の長さで決め、遠いほど画素数を減らす。7×7 の点を幅に合わせた間隔で読み、
// 深さが大きく違う点は混ぜない。しぶきの無い画素（0）は 0 のまま残す
// ============================================================

Texture2D<float> gDepthInput : register(t0);
RWTexture2D<float> gDepthOutput : register(u0);

cbuffer WaterSpraySmoothConstants : register(b0)
{
    uint2 gSmoothScreenSize;        // 画面の大きさ [px]
    float gSmoothPixelsPerMeter;    // 距離 1m で 1m が占める画素数
    float gSmoothWorldRadius;       // ならす幅 [m]
    float gSmoothDepthFalloff;      // 混ぜる深さの差の目安 [m]
    float3 gSmoothPad;
};

/// @brief 片側に読む点の数
static const int kSmoothTaps = 3;
/// @brief ならす幅の上限 [px]
static const float kSmoothMaxRadius = 24.0f;

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (any(dispatchThreadId.xy >= gSmoothScreenSize))
    {
        return;
    }
    const int2 pixel = int2(dispatchThreadId.xy);
    const float center = gDepthInput.Load(int3(pixel, 0));
    if (center <= 0.0f)
    {
        gDepthOutput[pixel] = 0.0f;
        return;
    }

    const float radiusPixels = clamp(gSmoothWorldRadius * gSmoothPixelsPerMeter / center, 1.0f, kSmoothMaxRadius);
    const float stride = max(radiusPixels / (float)kSmoothTaps, 1.0f);
    const float invTwoSigma2 = 1.0f / (2.0f * 0.5f * (float)(kSmoothTaps * kSmoothTaps));
    const float invFalloff2 = 1.0f / max(gSmoothDepthFalloff * gSmoothDepthFalloff, 1.0e-6f);
    const int2 maxPixel = int2(gSmoothScreenSize) - 1;

    float sum = 0.0f;
    float weightSum = 0.0f;
    [unroll]
    for (int y = -kSmoothTaps; y <= kSmoothTaps; ++y)
    {
        [unroll]
        for (int x = -kSmoothTaps; x <= kSmoothTaps; ++x)
        {
            const int2 samplePixel = clamp(pixel + int2(round(float2(x, y) * stride)), int2(0, 0), maxPixel);
            const float value = gDepthInput.Load(int3(samplePixel, 0));
            if (value <= 0.0f)
            {
                continue;
            }
            const float difference = value - center;
            const float weight = exp(-(float)(x * x + y * y) * invTwoSigma2) * exp(-difference * difference * invFalloff2);
            sum += value * weight;
            weightSum += weight;
        }
    }
    gDepthOutput[pixel] = sum / max(weightSum, 1.0e-6f);
}
