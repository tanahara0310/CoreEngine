// ============================================================
// 岸の泡の寄せ・引きに使う、ならした海底の高さ
// ============================================================

Texture2D<float> gSeabedHeight : register(t0);
RWTexture2D<float> gSmoothSeabedOutput : register(u0);
SamplerState gLinearClamp : register(s0);

cbuffer WaterShoreSeabedSmoothConstants : register(b0)
{
    uint gOutputResolution;  // 出力の一辺のテクセル数
    uint gInputResolution;   // 海底の高さの一辺のテクセル数
    float2 gSmoothPad;
};

/// @brief 出力 1 テクセルが覆う海底の高さのテクセルの平均を書く
/// @details 2×2 テクセルの中心をバイリニアで読むと 4 テクセルの平均になるので、それを並べて平均する
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= gOutputResolution || dispatchThreadId.y >= gOutputResolution)
    {
        return;
    }

    const uint block = gInputResolution / gOutputResolution;
    const uint taps = max(block / 2u, 1u);
    const float2 blockOrigin = float2(dispatchThreadId.xy * block);
    float sum = 0.0f;
    for (uint y = 0; y < taps; ++y)
    {
        for (uint x = 0; x < taps; ++x)
        {
            const float2 texel = blockOrigin + float2(x, y) * 2.0f + 1.0f;
            sum += gSeabedHeight.SampleLevel(gLinearClamp, texel / (float)gInputResolution, 0.0f);
        }
    }
    gSmoothSeabedOutput[dispatchThreadId.xy] = sum / (float)(taps * taps);
}
