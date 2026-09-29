// ============================================================
// 水面反射の元画像の縮小段（WaterReflectionColorPyramid・WaterReflectionRayTracingManager 専用）
// ------------------------------------------------------------
// 1 段上の画像を 2x2 平均して次の段を作る。RTWaterReflection は反射レイが当たった点を、
// 水面の荒さに合った段から引いてぼかす。
// 段 0 は水面を描く前の画面の写しから作り、水域の水面より下の画素（まだ水が描かれていない
// 海底など）を外す。反射に水の下の物は映らないので、ぼかしてもその色を混ぜない。
// 出力は乗算済みアルファ: rgb = 水面より上の画素の色の平均 × a、a = 水面より上の画素の割合。
// ============================================================

#include "../../Include/Common/DepthReconstruction.hlsli"

Texture2D<float4> gPyramidSource : register(t0);
Texture2D<float> gPyramidSceneDepth : register(t1);
RWTexture2D<float4> gPyramidDest : register(u0);
SamplerState gLinearClamp : register(s0);

cbuffer PyramidConstants : register(b0)
{
    uint gDestWidth;
    uint gDestHeight;
    float gDestInvWidth;
    float gDestInvHeight;
    uint gFromSceneColor;           // 1 = 段 0（画面の写しから作り、水面より下の画素を外す）
    float gWaterHeight;             // 平らな水面の高さ
    float2 gWaterRegionCenterXZ;    // 水域の中心（XZ）
    float2 gWaterRegionHalfExtentXZ; // 水域の半径（XZ）。0 なら水域の制限なし
    float2 gPyramidPad;
    float4x4 gInvViewProjection;    // 深度から位置を戻す
};

/// @brief fp16 の範囲外と負の値を落とす（1 画素の異常値が縮小で周りへ広がらないようにする）
float4 SanitizeColor(float4 color)
{
    return min(max(color, 0.0f), 65000.0f);
}

/// @brief 画面の写しの画素が反射に映りうるか（水域の水面より下なら 0）
float ComputeReflectableWeight(uint2 pixel, uint2 sourceSize)
{
    const float depth = gPyramidSceneDepth.Load(int3(pixel, 0));
    if (IsBackgroundDepth(depth))
    {
        return 1.0f;
    }
    const float2 uv = (float2(pixel) + 0.5f) / float2(sourceSize);
    const float3 worldPos = ReconstructWorldPosition(ScreenUVToNDC(uv), depth, gInvViewProjection);
    if (worldPos.y >= gWaterHeight)
    {
        return 1.0f;
    }
    const bool hasRegion = any(gWaterRegionHalfExtentXZ > 0.0f);
    const float2 offset = abs(worldPos.xz - gWaterRegionCenterXZ);
    const bool insideRegion = !hasRegion || all(offset <= gWaterRegionHalfExtentXZ);
    return insideRegion ? 0.0f : 1.0f;
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= gDestWidth || dispatchThreadId.y >= gDestHeight)
    {
        return;
    }

    if (gFromSceneColor != 0)
    {
        // 段 0: 画面の写しの 2x2 画素を、映りうる画素だけ乗算済みアルファで平均する
        uint sourceWidth = 1;
        uint sourceHeight = 1;
        gPyramidSource.GetDimensions(sourceWidth, sourceHeight);
        const uint2 sourceSize = uint2(sourceWidth, sourceHeight);
        const uint2 basePixel = dispatchThreadId.xy * 2u;
        float4 sum = float4(0.0f, 0.0f, 0.0f, 0.0f);
        [unroll]
        for (uint i = 0; i < 4u; ++i)
        {
            const uint2 pixel = min(basePixel + uint2(i & 1u, i >> 1u), sourceSize - 1u);
            const float weight = ComputeReflectableWeight(pixel, sourceSize);
            const float3 color = SanitizeColor(gPyramidSource.Load(int3(pixel, 0))).rgb;
            sum += float4(color * weight, weight);
        }
        gPyramidDest[dispatchThreadId.xy] = sum * 0.25f;
        return;
    }

    // 段 1 以降: 出力 1 画素の中心 ＝ 入力 2x2 画素の中心なので、双線形 1 回で 2x2 の平均になる
    // （乗算済みアルファのまま平均してよい）
    const float2 uv = (float2(dispatchThreadId.xy) + 0.5f) * float2(gDestInvWidth, gDestInvHeight);
    gPyramidDest[dispatchThreadId.xy] = gPyramidSource.SampleLevel(gLinearClamp, uv, 0.0f);
}
