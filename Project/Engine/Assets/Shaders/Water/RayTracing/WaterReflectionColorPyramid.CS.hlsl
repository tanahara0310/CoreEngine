// ============================================================
// 水面反射の元画像の縮小段（WaterReflectionRayTracingManager 専用）
// ============================================================

#include "../../Include/Common/DepthReconstruction.hlsli"

// 段 0 は水面を描く前の画面の写し、段 1 以降は 1 段上の色
Texture2D<float4> gPyramidSource : register(t0);
Texture2D<float> gPyramidSceneDepth : register(t1);
// 1 段上の空の割合（段 1 以降だけ読む）
Texture2D<float> gPyramidSkySource : register(t2);
// 物の色（乗算済みアルファ）: rgb = 物の画素の色の平均 × a、a = 物の画素の割合
RWTexture2D<float4> gPyramidDest : register(u0);
// 空の画素の割合
RWTexture2D<float> gPyramidSkyDest : register(u1);
SamplerState gLinearClamp : register(s0);

cbuffer PyramidConstants : register(b0)
{
    uint gDestWidth;
    uint gDestHeight;
    float gDestInvWidth;
    float gDestInvHeight;
    uint gFromSceneColor;           // 1 = 段 0（画面の写しから作り、画素を物・空・水面より下に分ける）
    float gWaterHeight;             // 平らな水面の高さ
    float2 gWaterRegionCenterXZ;    // 水域の中心（XZ）
    float2 gWaterRegionHalfExtentXZ; // 水域の半径（XZ）。0 なら水域の制限なし
    float2 gPyramidPad;
    float4x4 gInvViewProjection;    // 深度から位置を戻す
};

/// @brief fp16 の範囲外と負の値を落とす
float4 SanitizeColor(float4 color)
{
    return min(max(color, 0.0f), 65000.0f);
}

/// @brief XZ が水域の中か（水域の制限が無ければ常に中）
bool IsInsideWaterRegion(float2 worldXZ)
{
    const bool hasRegion = any(gWaterRegionHalfExtentXZ > 0.0f);
    const float2 offset = abs(worldXZ - gWaterRegionCenterXZ);
    return !hasRegion || all(offset <= gWaterRegionHalfExtentXZ);
}

/// @brief 画面の写しの画素を分ける
/// @return x = 水面より上の物なら 1、y = 空なら 1（水域の水面より下はどちらも 0）
float2 ClassifySourcePixel(uint2 pixel, uint2 sourceSize)
{
    const float depth = gPyramidSceneDepth.Load(int3(pixel, 0));
    const float2 ndc = ScreenUVToNDC((float2(pixel) + 0.5f) / float2(sourceSize));
    if (IsBackgroundDepth(depth))
    {
        // 背景でも、視線が水域の水面に当たる画素は水面の下（海底の無い深い所）
        const float3 nearPos = ReconstructWorldPosition(ndc, 0.0f, gInvViewProjection);
        const float3 viewDir = ReconstructWorldPosition(ndc, 0.5f, gInvViewProjection) - nearPos;
        if (viewDir.y < 0.0f && nearPos.y > gWaterHeight)
        {
            const float3 waterHit = nearPos + viewDir * ((gWaterHeight - nearPos.y) / viewDir.y);
            if (IsInsideWaterRegion(waterHit.xz))
            {
                return float2(0.0f, 0.0f);
            }
        }
        return float2(0.0f, 1.0f);
    }
    const float3 worldPos = ReconstructWorldPosition(ndc, depth, gInvViewProjection);
    if (worldPos.y >= gWaterHeight)
    {
        return float2(1.0f, 0.0f);
    }
    return IsInsideWaterRegion(worldPos.xz) ? float2(0.0f, 0.0f) : float2(1.0f, 0.0f);
}

/// @brief 1 段上を 2x2 平均して次の段の色と空の割合を書く
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= gDestWidth || dispatchThreadId.y >= gDestHeight)
    {
        return;
    }

    if (gFromSceneColor != 0)
    {
        // 段 0: 画面の写しの 2x2 画素を物・空・水面より下に分け、物の色（乗算済みアルファ）と空の割合を平均する
        uint sourceWidth = 1;
        uint sourceHeight = 1;
        gPyramidSource.GetDimensions(sourceWidth, sourceHeight);
        const uint2 sourceSize = uint2(sourceWidth, sourceHeight);
        const uint2 basePixel = dispatchThreadId.xy * 2u;
        float4 geometrySum = float4(0.0f, 0.0f, 0.0f, 0.0f);
        float skySum = 0.0f;
        [unroll]
        for (uint i = 0; i < 4u; ++i)
        {
            const uint2 pixel = min(basePixel + uint2(i & 1u, i >> 1u), sourceSize - 1u);
            const float2 classification = ClassifySourcePixel(pixel, sourceSize);
            const float3 color = SanitizeColor(gPyramidSource.Load(int3(pixel, 0))).rgb;
            geometrySum += float4(color * classification.x, classification.x);
            skySum += classification.y;
        }
        gPyramidDest[dispatchThreadId.xy] = geometrySum * 0.25f;
        gPyramidSkyDest[dispatchThreadId.xy] = skySum * 0.25f;
        return;
    }

    // 段 1 以降: 入力 2x2 画素の中心を双線形で 1 回引いて 2x2 の平均にする（乗算済みアルファのまま）
    const float2 uv = (float2(dispatchThreadId.xy) + 0.5f) * float2(gDestInvWidth, gDestInvHeight);
    gPyramidDest[dispatchThreadId.xy] = gPyramidSource.SampleLevel(gLinearClamp, uv, 0.0f);
    gPyramidSkyDest[dispatchThreadId.xy] = gPyramidSkySource.SampleLevel(gLinearClamp, uv, 0.0f);
}
