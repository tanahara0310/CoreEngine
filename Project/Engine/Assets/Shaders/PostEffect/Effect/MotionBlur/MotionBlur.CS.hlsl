// MotionBlur.CS.hlsl - ギャザー本体（McGuire 方式の再構成フィルタ）
//
// NeighborMax の方向に沿って画面をサンプリングし、
// 「そのサンプルの画素が露光時間中に自分の上を通過したか」を
// 速度と深度で重み付けして合成する。
//
// 3 つの重み項の意味:
//   f * Cone(dist, サンプル速度) … 手前で動くサンプルが自分の上へ流れてくる（前景のにじみ出し）
//   b * Cone(dist, 自分の速度)   … 自分が動いて半透明になり、奥のサンプルが透けて見える
//   Cylinder × Cylinder × 2      … 両方動いているときの相互汚染
//
// 深度は Outline と同じ非反転 [0,1] を線形化して使う（近いほど小さい）

#include "MotionBlurCommon.hlsli"

Texture2D<float4> gTexture : register(t0);     // シーンカラー（リニアHDR）
Texture2D<float2> gVelocity : register(t1);    // G-Buffer MotionVector（NDC差分）
Texture2D<float> gDepth : register(t2);        // シーン深度（NDC [0,1]）
Texture2D<float2> gNeighborMax : register(t3); // 隣接タイル最大速度[px]
RWTexture2D<float4> gOutput : register(u0);

cbuffer MotionBlurParams : register(b0)
{
    uint2 screenSize;
    uint2 tileCount;       // タイルバッファの実寸法
    float shutterFraction; // シャッター開角度/360
    float maxBlurPixels;   // ブラー最大距離[px]
    uint sampleCount;      // 探索サンプル数
    uint tileSize;         // タイル一辺[px]
    float nearPlane;
    float farPlane;
    float depthExtent;     // 前後判定のソフト境界幅[m]
    float pad;
};

/// @brief NDC深度をビュー空間の線形距離へ（Outline.CS と同式）
float LinearDepth(float ndcDepth)
{
    return (nearPlane * farPlane) / (farPlane - ndcDepth * (farPlane - nearPlane));
}

/// @brief depthA が depthB より手前なら 1、depthExtent 以上奥なら 0
float InFront(float depthA, float depthB)
{
    return saturate(1.0f - (depthA - depthB) / depthExtent);
}

/// @brief 速度 speed で動く画素のブラーが距離 dist まで届く度合い
float Cone(float dist, float speed)
{
    return saturate(1.0f - dist / max(speed, 1e-4f));
}

/// @brief Cone のソフト末端版（両者が動くケースの相互項に使う）
float Cylinder(float dist, float speed)
{
    return 1.0f - smoothstep(0.95f * speed, 1.05f * speed + 1e-4f, dist);
}

/// @brief Interleaved Gradient Noise（ToneMapping のディザと同じ関数）
/// @details サンプル位置を画素ごとにずらし、少ないサンプル数の縞をノイズへ散らす
float InterleavedGradientNoise(float2 pixelPosition)
{
    return frac(52.9829189f * frac(dot(pixelPosition, float2(0.06711056f, 0.00583715f))));
}

static const uint kGroupSize = 8;

[numthreads(kGroupSize, kGroupSize, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    if (dispatchId.x >= screenSize.x || dispatchId.y >= screenSize.y)
    {
        return;
    }

    const int2 coord = int2(dispatchId.xy);
    const float4 centerColor = gTexture.Load(int3(coord, 0));

    // タイルの最大速度が閾値未満なら、このタイルを通過する動きは無い。早期リターンで
    // 静止画面のコストをほぼゼロにする（画面の大半はここで抜ける）
    const uint2 tile = min(uint2(coord) / tileSize, tileCount - 1);
    const float2 vMax = gNeighborMax.Load(int3(tile, 0));
    const float vMaxLen = length(vMax);
    if (vMaxLen <= kMinVelocityPixels)
    {
        gOutput[coord] = centerColor;
        return;
    }

    const float2 screenSizeF = float2(screenSize);
    const float2 vCenter = ScaleAndClampVelocity(
        NdcVelocityToPixels(gVelocity.Load(int3(coord, 0)), screenSizeF),
        shutterFraction, maxBlurPixels);
    const float centerSpeed = max(length(vCenter), kMinVelocityPixels);
    const float centerDepth = LinearDepth(gDepth.Load(int3(coord, 0)));

    // 中心の自己寄与。速く動く画素ほど「その場に留まっていた時間」が短いので寄与を下げる
    float totalWeight = float(sampleCount) / (maxBlurPixels * centerSpeed);
    float3 sum = centerColor.rgb * totalWeight;

    const float jitter = InterleavedGradientNoise(float2(coord)) - 0.5f;

    for (uint i = 0; i < sampleCount; ++i)
    {
        // タイル最大速度の直線に沿って [-1, +1] を等分割（ジッタで画素ごとに位相をずらす）
        const float t = lerp(-1.0f, 1.0f, (float(i) + jitter + 1.0f) / (float(sampleCount) + 1.0f));
        int2 sampleCoord = int2(round(float2(coord) + vMax * t));
        sampleCoord = clamp(sampleCoord, int2(0, 0), int2(screenSize) - 1);

        const float dist = length(float2(sampleCoord - coord));

        const float2 vSample = ScaleAndClampVelocity(
            NdcVelocityToPixels(gVelocity.Load(int3(sampleCoord, 0)), screenSizeF),
            shutterFraction, maxBlurPixels);
        const float sampleSpeed = max(length(vSample), kMinVelocityPixels);
        const float sampleDepth = LinearDepth(gDepth.Load(int3(sampleCoord, 0)));

        const float front = InFront(sampleDepth, centerDepth); // サンプルが手前
        const float back  = InFront(centerDepth, sampleDepth); // 中心が手前

        const float alpha = front * Cone(dist, sampleSpeed)
                          + back * Cone(dist, centerSpeed)
                          + Cylinder(dist, sampleSpeed) * Cylinder(dist, centerSpeed) * 2.0f;

        totalWeight += alpha;
        sum += gTexture.Load(int3(sampleCoord, 0)).rgb * alpha;
    }

    gOutput[coord] = float4(sum / totalWeight, centerColor.a);
}
