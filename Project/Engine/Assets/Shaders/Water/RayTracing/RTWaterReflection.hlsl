// ============================================================
// DXR 水面反射シェーダー
// フラットな水面平面（+FFT波）を近似し、反射レイの最初のヒット位置を
// SceneColor に再投影して水面反射用のカラー出力を生成する。
// RTWaterRefraction.hlsl の対称形（refract→reflect、バイアス方向反転）。
// ミス／画面外／深度不一致のときはアルファ < 0.5 のフォールバック信号を出し、
// Water.PS.hlsl 側が空環境マップ（gSkyEnvironmentMap）へ落とす。
// ============================================================

#include "RTWaterSurfaceCommon.hlsli"
#include "../../Include/Common/DepthReconstruction.hlsli"

RWTexture2D<float4> gReflectionOutput : register(u0);
RaytracingAccelerationStructure gScene : register(t0);
Texture2D<float> gSceneDepth : register(t1);
Texture2D<float4> gSceneColor : register(t2);
Texture2DArray<float4> gFFTOceanDisplacement : register(t3);
Texture2DArray<float4> gFFTOceanNormal : register(t4);

cbuffer WaterReflectionConstants : register(b0)
{
    float4x4 gViewProjection;
    float4x4 gInvViewProjection; // 深度復元用
    float3 gCameraPosition;
    float gWaterHeight;
    float gSurfaceBias;
    float gMaxRayDistance;
    float gUnused0; // （屈折の refractionEta 相当。反射では未使用。レイアウト維持）
    float gUnused1; // （屈折の absorptionCoeff 相当。反射では未使用）
    float gScreenWidth;
    float gScreenHeight;
    float gMaxReflectionOffsetPixels;
    uint gFFTOceanEnabled;
    float gFFTOceanPatchLength;
    uint gFFTOceanResolution;
    float gDebugDisplayScale;
    uint gDebugViewMode;
    float2 gFFTOceanUVScale;
    float2 gFFTOceanUVOffset;
};

struct ReflectionPayload
{
    float hitT;
    float hitFlag;
};

#ifdef __INTELLISENSE__
#define RAY_FLAG_NONE 0x0
void TraceRay(
    RaytracingAccelerationStructure scene,
    uint rayFlags,
    uint instanceInclusionMask,
    uint rayContributionToHitGroupIndex,
    uint multiplierForGeometryContributionToHitGroupIndex,
    uint missShaderIndex,
    RayDesc ray,
    inout ReflectionPayload payload);
#endif

// 失敗理由コード（[0, 0.5) の範囲）。Water.PS 側は alpha >= 0.5 を成功と判定する。
static const float kRTReasonBackground = 1.0f / 255.0f;
static const float kRTReasonNearZeroSceneDistance = 2.0f / 255.0f;
static const float kRTReasonParallelToWater = 3.0f / 255.0f;
static const float kRTReasonInvalidPlaneIntersection = 4.0f / 255.0f;
static const float kRTReasonInvalidReflectVector = 5.0f / 255.0f;
static const float kRTReasonTraceMiss = 6.0f / 255.0f;
static const float kRTReasonInvalidClip = 7.0f / 255.0f;
static const float kRTReasonDepthMismatch = 9.0f / 255.0f;

// 成功アルファ。反射は水柱の光路長を持たないため単純に 1.0 を返す
// （画面端フェードは Water.PS 側でなく、ここで色に織り込む）。
static const float kRTSuccessAlpha = 1.0f;

float4 MakeFallbackOutput(float reasonCode)
{
    // 失敗時 RGB は使われない（Water.PS が空環境マップで置き換える）。0 で埋める。
    return float4(0.0f, 0.0f, 0.0f, reasonCode);
}

float ComputeScreenBoundsFade(float2 uv)
{
    const float2 screenSize = float2(gScreenWidth, gScreenHeight);
    const float2 outsideDistancePixels = abs(uv - saturate(uv)) * screenSize;
    const float outsidePixels = max(outsideDistancePixels.x, outsideDistancePixels.y);
    const float fadeMarginPixels = 32.0f;
    return 1.0f - smoothstep(0.0f, fadeMarginPixels, outsidePixels);
}

bool UseFFTOceanSurface()
{
    return gSurfaceSimulationType == kWaterSurfaceModelTypeFFTOcean
        && gFFTOceanEnabled != 0
        && gFFTOceanResolution > 0
        && gFFTOceanPatchLength > 1.0e-4f;
}

float3 EvaluateReflectionWaterOffset(float2 worldXZ)
{
    if (!UseFFTOceanSurface())
    {
        return EvaluateWaterOffset(worldXZ);
    }
    return SampleFFTOceanCascadeDisplacement(gFFTOceanDisplacement, worldXZ, gFFTOceanResolution);
}

float3 EvaluateReflectionWaterNormal(float2 worldXZ)
{
    if (!UseFFTOceanSurface())
    {
        return EvaluateWaterNormal(worldXZ);
    }
    return SampleFFTOceanCascadeNormal(gFFTOceanNormal, worldXZ, gFFTOceanResolution);
}

[shader("raygeneration")]
void RTWaterReflectionRayGen()
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    float ndcDepth = gSceneDepth.Load(int3(launchIndex, 0));

    // 背景（空）ピクセルは水面ではないので反射不要。フォールバック信号を出す。
    if (IsBackgroundDepth(ndcDepth))
    {
        gReflectionOutput[launchIndex] = MakeFallbackOutput(kRTReasonBackground);
        return;
    }

    float2 screenUV = (float2(launchIndex) + 0.5f.xx) / float2(gScreenWidth, gScreenHeight);
    float3 worldPos = ReconstructWorldPosition(ScreenUVToNDC(screenUV), ndcDepth, gInvViewProjection);

    float3 cameraToScene = worldPos - gCameraPosition;
    float sceneDistance = length(cameraToScene);
    if (sceneDistance <= 1.0e-4f)
    {
        gReflectionOutput[launchIndex] = MakeFallbackOutput(kRTReasonNearZeroSceneDistance);
        return;
    }

    float3 primaryDir = cameraToScene / sceneDistance;
    float denom = primaryDir.y;
    if (abs(denom) <= 1.0e-5f)
    {
        gReflectionOutput[launchIndex] = MakeFallbackOutput(kRTReasonParallelToWater);
        return;
    }

    // フラット平面での交点をシード初期値とし、波を反映して反復精密化する
    // （RTWaterRefraction.hlsl と同じロジック）。
    float tSurfaceSeed = (gSurfaceWaterHeight - gCameraPosition.y) / denom;
    if (!isfinite(tSurfaceSeed))
    {
        gReflectionOutput[launchIndex] = MakeFallbackOutput(kRTReasonInvalidPlaneIntersection);
        return;
    }
    float tSeedClamped = clamp(tSurfaceSeed, 0.0f, sceneDistance);

    float3 waterPos = gCameraPosition + primaryDir * tSeedClamped;
    float safeDenom = denom;
    if (abs(safeDenom) < 1.0e-4f)
    {
        safeDenom = (safeDenom < 0.0f) ? -1.0e-4f : 1.0e-4f;
    }
    [unroll]
    for (int iteration = 0; iteration < 3; ++iteration)
    {
        float3 waveOffset = EvaluateReflectionWaterOffset(waterPos.xz);
        float surfaceY = gSurfaceWaterHeight + waveOffset.y;
        float deltaY = waterPos.y - surfaceY;
        waterPos -= primaryDir * (deltaY / safeDenom);
    }

    // 水面が opaque シーン点より手前にあるピクセルだけ反射する（＝水面が見えている領域）。
    float tRefined = dot(waterPos - gCameraPosition, primaryDir);
    if (tRefined <= 1.0e-4f || tRefined >= sceneDistance)
    {
        gReflectionOutput[launchIndex] = MakeFallbackOutput(kRTReasonInvalidPlaneIntersection);
        return;
    }

    float3 waterNormal = EvaluateReflectionWaterNormal(waterPos.xz);
    // 視線（primaryDir）を水面法線で鏡面反射。カメラは水面を上から見下ろすため
    // primaryDir は下向き、reflectedDir は上向き（空・水上ジオメトリ方向）になる。
    float3 reflectedDir = reflect(primaryDir, waterNormal);
    if (dot(reflectedDir, reflectedDir) <= 1.0e-6f)
    {
        gReflectionOutput[launchIndex] = MakeFallbackOutput(kRTReasonInvalidReflectVector);
        return;
    }

    RayDesc ray;
    // 反射レイは空気側（+waterNormal）へ進むため、自己交差回避のバイアスも +waterNormal。
    ray.Origin = waterPos + waterNormal * gSurfaceBias;
    ray.Direction = normalize(reflectedDir);
    ray.TMin = 0.001f;
    ray.TMax = gMaxRayDistance;

    ReflectionPayload payload;
    payload.hitT = 0.0f;
    payload.hitFlag = 0.0f;

    // 反射は最も近い交差面の色が必要なため最近接ヒット（RAY_FLAG_NONE）。
    TraceRay(gScene, RAY_FLAG_NONE, 0xFF, 0, 1, 0, ray, payload);

    if (payload.hitFlag < 0.5f)
    {
        // 反射レイが何にも当たらず空へ抜けた → 空環境マップへフォールバック。
        gReflectionOutput[launchIndex] = MakeFallbackOutput(kRTReasonTraceMiss);
        return;
    }

    float3 hitWorldPos = ray.Origin + ray.Direction * payload.hitT;
    float4 clip = mul(float4(hitWorldPos, 1.0f), gViewProjection);
    if (clip.w <= 1.0e-5f)
    {
        gReflectionOutput[launchIndex] = MakeFallbackOutput(kRTReasonInvalidClip);
        return;
    }

    float3 ndc = clip.xyz / clip.w;
    float2 uv = ndc.xy * float2(0.5f, -0.5f) + 0.5f;

    float edgeFade = ComputeScreenBoundsFade(uv);
    if (edgeFade <= 1.0e-4f)
    {
        gReflectionOutput[launchIndex] = MakeFallbackOutput(kRTReasonInvalidClip);
        return;
    }

    float2 clampedUV = saturate(uv);
    float2 reflectedUV = clampedUV;
    if (gMaxReflectionOffsetPixels > 0.0f)
    {
        float2 maxOffset = max(gMaxReflectionOffsetPixels, 0.0f) / float2(gScreenWidth, gScreenHeight);
        float2 uvOffset = clamp(clampedUV - screenUV, -maxOffset, maxOffset);
        reflectedUV = saturate(screenUV + uvOffset);
    }

    uint2 sampleCoord = uint2(reflectedUV * float2(gScreenWidth, gScreenHeight));
    sampleCoord = min(sampleCoord, uint2(gScreenWidth - 1.0f, gScreenHeight - 1.0f));

    // スクリーン空間オクルージョン判定（SSR の要）。反射ヒット点の深度が、
    // 再投影先ピクセルの SceneDepth と食い違う場合、その反射点は画面上で
    // 別の物体に隠れており色を取得できない → フォールバック。
    float sampledDepth = gSceneDepth.Load(int3(sampleCoord, 0));
    if (IsBackgroundDepth(sampledDepth))
    {
        // 再投影先が空（背景）＝反射先は空。SceneColor の空をそのまま使える。
        float3 skyColor = gSceneColor.Load(int3(sampleCoord, 0)).rgb;
        gReflectionOutput[launchIndex] = float4(skyColor * edgeFade, kRTSuccessAlpha);
        return;
    }

    float3 sampledWorldPos = ReconstructWorldPosition(ScreenUVToNDC(reflectedUV), sampledDepth, gInvViewProjection);
    float sampledViewDistance = length(sampledWorldPos - gCameraPosition);
    float hitViewDistance = length(hitWorldPos - gCameraPosition);
    float depthMismatch = abs(sampledViewDistance - hitViewDistance);
    float depthMismatchThreshold = max(0.08f, hitViewDistance * 0.03f);

    if (depthMismatch > depthMismatchThreshold)
    {
        gReflectionOutput[launchIndex] = MakeFallbackOutput(kRTReasonDepthMismatch);
        return;
    }

    float3 reflectedColor = gSceneColor.Load(int3(sampleCoord, 0)).rgb;
    gReflectionOutput[launchIndex] = float4(reflectedColor * edgeFade, kRTSuccessAlpha);
}

[shader("miss")]
void RTWaterReflectionMiss(inout ReflectionPayload payload)
{
    payload.hitT = 0.0f;
    payload.hitFlag = 0.0f;
}

[shader("closesthit")]
void RTWaterReflectionClosestHit(
    inout ReflectionPayload payload,
    in BuiltInTriangleIntersectionAttributes attr)
{
    payload.hitT = RayTCurrent();
    payload.hitFlag = 1.0f;
}
