// ============================================================
// 水の波紋の源を格子へ入れる
// 源は体積を保つガウスのくぼみ h(r) = -深さ × exp(-r² / 2σ²)。
// 段 L はくぼみを幅 s_L でぼかしたものから幅 s_{L+1} でぼかしたものを引いた帯を受け持ち、
// 全部の段を足すと元のくぼみに戻る（いちばん粗い段は引かない）。
// くぼみは h_n と h_{n-1} の両方へ足し、止まった水面を押し下げて放した状態から波を進める。
// ============================================================

#include "../Common/WaterRippleGrid.hlsli"

static const uint kRippleMaxSourceCount = 256;

cbuffer WaterRippleInjectConstants : register(b0)
{
    float2 gInjectOriginXZ;    // 段の最小の角 [m]
    float gInjectCellSize;     // 1 マスの幅 [m]
    uint gInjectLevel;
    uint gInjectPair;          // 今の組
    uint gInjectSourceCount;
    float gInjectLowerBlur;    // この段の帯の細かい側のぼかしの幅 [m]
    float gInjectUpperBlur;    // この段の帯の粗い側のぼかしの幅 [m]（0 なら引かない）
    int2 gInjectRectMin;       // 書くマスの範囲の最小の角
    uint2 gInjectRectSize;     // 書くマスの範囲の大きさ
    float4 gInjectSources[kRippleMaxSourceCount]; // (x, z, σ [m], 深さ [m])
};

RWTexture2DArray<float> gRippleState : register(u0);

/// @brief 幅 blur でぼかしたくぼみの高さ（体積は同じ）
float BlurredDent(float r2, float sigma2, float depth, float blur)
{
    const float spread2 = sigma2 + blur * blur;
    return -depth * (sigma2 / spread2) * exp(-0.5f * r2 / spread2);
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (any(dispatchThreadId.xy >= gInjectRectSize))
    {
        return;
    }
    const int2 cell = gInjectRectMin + int2(dispatchThreadId.xy);
    const float2 positionXZ = gInjectOriginXZ + (float2(cell) + 0.5f) * gInjectCellSize;

    float delta = 0.0f;
    for (uint i = 0; i < gInjectSourceCount; ++i)
    {
        const float4 source = gInjectSources[i];
        const float2 offset = positionXZ - source.xy;
        const float r2 = dot(offset, offset);
        const float sigma2 = source.z * source.z;
        const float widest = sigma2 + max(gInjectLowerBlur, gInjectUpperBlur) * max(gInjectLowerBlur, gInjectUpperBlur);
        if (r2 > 16.0f * widest)
        {
            continue;
        }
        delta += BlurredDent(r2, sigma2, source.w, gInjectLowerBlur);
        if (gInjectUpperBlur > 0.0f)
        {
            delta -= BlurredDent(r2, sigma2, source.w, gInjectUpperBlur);
        }
    }

    const uint3 heightTexel = uint3(cell, RippleStateSlice(gInjectLevel, gInjectPair, 0));
    const uint3 previousTexel = uint3(cell, RippleStateSlice(gInjectLevel, gInjectPair, 1));
    gRippleState[heightTexel] = gRippleState[heightTexel] + delta;
    gRippleState[previousTexel] = gRippleState[previousTexel] + delta;
}
