// ============================================================
// RT の水面反射・屈折が共有するレイのたどり方
// 主のレイ・遮る物の判定・当たった点の画面への投影。
// 加速構造は引数で受け取る（このヘッダーはリソースを宣言しない）。
// ============================================================
#ifndef RT_WATER_TRACE_HLSLI
#define RT_WATER_TRACE_HLSLI

#include "RTWaterSurfaceCommon.hlsli"
#include "../../Include/RayTracing/RTHitShading.hlsli"
#include "../../Include/RayTracing/RTTrace.hlsli"

/// @brief 点から向きへ遮る物が無いかを返す（1 = 無い / 0 = 遮られる）
/// @param coneWidth 点でのレイの広がりの幅（m。アルファで抜ける所を調べるテクスチャのミップの選択に使う）
float TraceVisibility(
    RaytracingAccelerationStructure scene, float3 origin, float3 direction, float maxDistance, float coneWidth)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.001f;
    ray.TMax = maxDistance;
    return TraceRTOcclusion(scene, GetHitShadingTables(), ray, kRTInstanceMaskShadowCaster, coneWidth, 0.0f)
        ? 0.0f : 1.0f;
}

/// @brief 水面から撃つ主のレイ（反射・屈折）の最も近い当たり（アルファで抜ける所は通り抜ける）
/// @param coneWidthAtOrigin レイの始点での広がりの幅（m）
/// @param coneSpread        レイの広がりの角度（rad。距離に比例して幅が増える）
RTTraceHit TraceWaterHit(
    RaytracingAccelerationStructure scene, RayDesc ray, float coneWidthAtOrigin, float coneSpread)
{
    return TraceRTClosestHit(
        scene, GetHitShadingTables(), ray, kRTInstanceMaskReflected, coneWidthAtOrigin, coneSpread);
}

/// @brief ワールドの点を画面へ投影し、画面の外へはみ出した距離に応じたフェードを返す
/// @param uv  投影したスクリーン UV（カメラの後ろのときは書き換えない）
/// @param ndc 投影した NDC（カメラの後ろのときは書き換えない）
/// @return 画面の中は 1、カメラの後ろは 0
float ProjectToScreen(
    float3 worldPos, float4x4 viewProjection, float2 screenSize, inout float2 uv, inout float3 ndc)
{
    const float4 clip = mul(float4(worldPos, 1.0f), viewProjection);
    float edgeFade = 0.0f;
    if (clip.w > 1.0e-5f)
    {
        ndc = clip.xyz / clip.w;
        uv = ndc.xy * float2(0.5f, -0.5f) + 0.5f;
        edgeFade = ComputeRTScreenBoundsFade(uv, screenSize);
    }
    return edgeFade;
}

/// @brief 投影先を、自分の画素から maxOffsetPixels 以内へ寄せる（0 以下なら寄せない）
/// @param clampedUV [0,1] にクランプした投影先
float2 LimitReprojectionOffset(float2 clampedUV, float2 screenUV, float maxOffsetPixels, float2 screenSize)
{
    float2 limitedUV = clampedUV;
    if (maxOffsetPixels > 0.0f)
    {
        const float2 maxOffset = max(maxOffsetPixels, 0.0f) / screenSize;
        const float2 uvOffset = clamp(clampedUV - screenUV, -maxOffset, maxOffset);
        limitedUV = saturate(screenUV + uvOffset);
    }
    return limitedUV;
}

/// @brief スクリーン UV の位置の画素の座標
uint2 ScreenUVToPixelCoord(float2 uv, float2 screenSize)
{
    const uint2 coord = uint2(uv * screenSize);
    return min(coord, uint2(screenSize - 1.0f));
}

#endif // RT_WATER_TRACE_HLSLI
