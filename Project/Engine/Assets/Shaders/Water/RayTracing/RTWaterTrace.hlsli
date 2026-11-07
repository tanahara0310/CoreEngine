// ============================================================
// RT の水面反射・屈折が共有するレイのたどり方
// ペイロード・遮る物の判定・抜ける材質の先への撃ち直し・当たった点の画面への投影。
// 加速構造は引数で受け取る（このヘッダーはリソースを宣言しない）。
// ============================================================
#ifndef RT_WATER_TRACE_HLSLI
#define RT_WATER_TRACE_HLSLI

#include "RTWaterSurfaceCommon.hlsli"
#include "../../Include/RayTracing/RTHitShading.hlsli"
#include "../../Include/RayTracing/RTInstanceMask.hlsli"

/// @brief 反射・屈折のペイロード（主のレイと影のレイで共通）
struct RTWaterHitPayload
{
    float hitT;
    float hitFlag;        // 1 = 当たった / 0 = 抜けた
    uint instanceIndex;   // InstanceID()（ヒットシェーディングの表の行）
    uint primitiveIndex;  // PrimitiveIndex()
    float2 barycentrics;  // 交点の重心座標
};

#ifdef __INTELLISENSE__
#define RAY_FLAG_NONE 0x0
#define RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH 0x4
#define RAY_FLAG_SKIP_CLOSEST_HIT_SHADER 0x8
void TraceRay(
    RaytracingAccelerationStructure scene,
    uint rayFlags,
    uint instanceInclusionMask,
    uint rayContributionToHitGroupIndex,
    uint multiplierForGeometryContributionToHitGroupIndex,
    uint missShaderIndex,
    RayDesc ray,
    inout RTWaterHitPayload payload);
#endif

/// @brief アルファで抜ける所に当たったときに、その先へ撃ち直す回数の上限
static const uint kMaxCutoutRetrace = 3;

/// @brief 何にも当たっていない状態のペイロード
RTWaterHitPayload MakeEmptyHitPayload()
{
    RTWaterHitPayload payload;
    payload.hitT = 0.0f;
    payload.hitFlag = 0.0f;
    payload.instanceIndex = 0;
    payload.primitiveIndex = 0;
    payload.barycentrics = float2(0.0f, 0.0f);
    return payload;
}

/// @brief 点から向きへ遮る物が無いかを返す（1 = 無い / 0 = 遮られる）
float TraceVisibility(RaytracingAccelerationStructure scene, float3 origin, float3 direction, float maxDistance)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.001f;
    ray.TMax = maxDistance;

    // 当たりはヒットシェーダーを通さずに終えるので、当たった扱いで初期化してミスだけが 0 に戻す
    RTWaterHitPayload payload = MakeEmptyHitPayload();
    payload.hitFlag = 1.0f;
    TraceRay(
        scene,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
        kRTInstanceMaskShadowCaster, 0, 1, 0, ray, payload);
    return 1.0f - payload.hitFlag;
}

/// @brief 当たった点から、抜ける材質（アルファ）を撃ち直して飛ばし、最初の不透明な面を探す
/// @param ray               当たったレイ（撃ち直すと TMin が進む）
/// @param payload           当たったレイの結果（当たっていること。撃ち直すと書き換わる）
/// @param coneWidthAtOrigin レイの始点での広がりの幅（m）
/// @param coneSpread        レイの広がりの角度（rad。距離に比例して幅が増える）
/// @param surface           見つかった面
/// @param coneWidth         見つかった面でのレイの広がりの幅（m）
/// @return 不透明な面が見つかったら true（抜ける材質の先で何にも当たらなかったときは false）
bool FindOpaqueHitSurface(
    RaytracingAccelerationStructure scene,
    inout RayDesc ray,
    inout RTWaterHitPayload payload,
    float coneWidthAtOrigin,
    float coneSpread,
    out RTHitSurface surface,
    out float coneWidth)
{
    surface = (RTHitSurface)0;
    coneWidth = 0.0f;
    [loop]
    for (uint attempt = 0; attempt <= kMaxCutoutRetrace; ++attempt)
    {
        coneWidth = coneWidthAtOrigin + coneSpread * payload.hitT;
        surface = FetchHitSurface(
            payload.instanceIndex, payload.primitiveIndex, payload.barycentrics, ray.Direction, coneWidth);
        if (!surface.cutout)
        {
            return true;
        }
        if (attempt == kMaxCutoutRetrace)
        {
            break;
        }

        ray.TMin = payload.hitT + 1.0e-3f;
        // TraceRay にはこの関数のローカル変数を渡す（inout の引数をそのまま渡すと -Od でコンパイルできない）
        RayDesc retraceRay = ray;
        RTWaterHitPayload retracePayload = payload;
        retracePayload.hitT = 0.0f;
        retracePayload.hitFlag = 0.0f;
        TraceRay(scene, RAY_FLAG_NONE, kRTInstanceMaskReflected, 0, 1, 0, retraceRay, retracePayload);
        payload = retracePayload;
        if (payload.hitFlag < 0.5f)
        {
            break;
        }
    }
    return false;
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

/// @brief ミスシェーダーの中身（何にも当たらなかった）
void RecordWaterTraceMiss(inout RTWaterHitPayload payload)
{
    payload.hitT = 0.0f;
    payload.hitFlag = 0.0f;
}

/// @brief 最近接ヒットシェーダーの中身（当たった距離と、当たった面を引く値）
void RecordWaterTraceHit(inout RTWaterHitPayload payload, BuiltInTriangleIntersectionAttributes attr)
{
    payload.hitT = RayTCurrent();
    payload.hitFlag = 1.0f;
    payload.instanceIndex = InstanceID();
    payload.primitiveIndex = PrimitiveIndex();
    payload.barycentrics = attr.barycentrics;
}

#endif // RT_WATER_TRACE_HLSLI
