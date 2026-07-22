// ============================================================
// DXR 水面コースティクスシェーダー
// 第2段階ではまず受光点ごとの簡易集光量を専用RT出力へ書き出す。
// ============================================================

#include "RTWaterSurfaceCommon.hlsli"
#include "../../Include/Common/DepthReconstruction.hlsli"

RWTexture2D<float4> gCausticsOutput : register(u0);
RaytracingAccelerationStructure gScene : register(t0);
Texture2D<float> gSceneDepth : register(t1); // WorldPosition ターゲット廃止に伴い深度から復元する
Texture2D<float4> gNormalRoughness : register(t2);
Texture2DArray<float4> gFFTOceanDisplacement : register(t3);
Texture2DArray<float4> gFFTOceanNormal : register(t4);

cbuffer WaterCausticsConstants : register(b0)
{
    float gMaxTraceDistance;
    float gSurfaceBias;
    float gIntensityScale;
    float gWaterHeight;
    float3 gLightDirection;
    float gScreenWidth;
    float gScreenHeight;
    uint gFFTOceanEnabled;
    float gFFTOceanPatchLength;
    uint gFFTOceanResolution;
    float gRefractiveIndex;
    float gDebugDisplayScale;
    uint gDebugViewMode;
    // シーンの実際のディレクショナルライトが無効な場合はコースティクスも出さない。
    // 以前はここが未使用の padding で、ライトを消してもコースティクスが消えなかった。
    uint gLightEnabled;
    float3 gLightColor;
    float gLightIntensity;
    // ワールドXZ → FFT テクスチャ UV の写像（ラスタ描画 FFTWater.VS と一致させる）
    float2 gFFTOceanUVScale;
    float2 gFFTOceanUVOffset;
    // 水面メッシュのワールドXZ範囲（WaterCausticsConstants と一致させること）。
    // コースティクスは解析的な無限水面として評価されるため、この矩形でマスクしないと
    // 水域の外（無限床など「水面高さより低い場所すべて」）にも集光模様が漏れる。
    float2 gRegionCenterXZ;
    float2 gRegionHalfExtentXZ;
    uint gRegionValid;
    float3 gRegionPadding;
    float4x4 gInvViewProj; // WorldPosition ターゲット廃止に伴う深度復元用
};

static const uint kRTCausticsDebugNone = 0;
static const uint kRTCausticsDebugShallowFade = 1;
static const uint kRTCausticsDebugMatchFactor = 2;
static const uint kRTCausticsDebugAttenuation = 3;
static const uint kRTCausticsDebugReceiverFacing = 4;
static const uint kRTCausticsDebugFinalIntensity = 5;

struct CausticsPayload
{
    float hitT;
    float hitFlag;
    float ndotL;
    float receiverDepth;
};

#ifdef __INTELLISENSE__
void TraceRay(
    RaytracingAccelerationStructure scene,
    uint rayFlags,
    uint instanceInclusionMask,
    uint rayContributionToHitGroupIndex,
    uint multiplierForGeometryContributionToHitGroupIndex,
    uint missShaderIndex,
    RayDesc ray,
    inout CausticsPayload payload);
#endif

float3 VisualizeScalar(float value)
{
    const float scaled = 1.0f - exp(-max(value, 0.0f) * max(gDebugDisplayScale, 1.0e-4f));
    return scaled.xxx;
}

float3 BuildCausticsDebugColor(
    uint debugViewMode,
    float shallowFade,
    float matchFactor,
    float attenuation,
    float receiverFacingFactor,
    float intensity)
{
    if (debugViewMode == kRTCausticsDebugShallowFade)
    {
        return VisualizeScalar(shallowFade);
    }

    if (debugViewMode == kRTCausticsDebugMatchFactor)
    {
        return VisualizeScalar(matchFactor);
    }

    if (debugViewMode == kRTCausticsDebugAttenuation)
    {
        return VisualizeScalar(attenuation);
    }

    if (debugViewMode == kRTCausticsDebugReceiverFacing)
    {
        return VisualizeScalar(receiverFacingFactor);
    }

    if (debugViewMode == kRTCausticsDebugFinalIntensity)
    {
        return VisualizeScalar(intensity);
    }

    return 0.0f.xxx;
}

bool UseFFTOceanSurface()
{
    return gSurfaceSimulationType == kWaterSurfaceModelTypeFFTOcean
        && gFFTOceanEnabled != 0
        && gFFTOceanResolution > 0
        && gFFTOceanPatchLength > 1.0e-4f;
}

float3 EvaluateCausticsWaterOffset(float2 worldXZ)
{
    if (!UseFFTOceanSurface())
    {
        return EvaluateWaterOffset(worldXZ);
    }

    return SampleFFTOceanCascadeDisplacement(gFFTOceanDisplacement, worldXZ, gFFTOceanResolution);
}

float3 EvaluateCausticsWaterNormal(float2 worldXZ)
{
    if (!UseFFTOceanSurface())
    {
        return EvaluateWaterNormal(worldXZ);
    }

    return SampleFFTOceanCascadeNormal(gFFTOceanNormal, worldXZ, gFFTOceanResolution);
}

[shader("raygeneration")]
void RTWaterCausticsRayGen()
{
    uint2 launchIndex = DispatchRaysIndex().xy;

    // ライトが無効（消灯）な場合はコースティクスも出さない。
    // 非RT版 WaterCaustics.PS.hlsl の gMainLightEnabled チェックと同じ扱い。
    if (gLightEnabled == 0)
    {
        gCausticsOutput[launchIndex] = 0.0f.xxxx;
        return;
    }

    float ndcDepth = gSceneDepth.Load(int3(launchIndex, 0));
    if (IsBackgroundDepth(ndcDepth))
    {
        gCausticsOutput[launchIndex] = 0.0f.xxxx;
        return;
    }

    float2 screenUV = (float2(launchIndex) + 0.5f.xx) / float2(gScreenWidth, gScreenHeight);
    float3 receiverWorldPos = ReconstructWorldPosition(ScreenUVToNDC(screenUV), ndcDepth, gInvViewProj);

    // 水面メッシュの XZ 範囲外（無限床など水域の外）にはコースティクスを落とさない。
    // 範囲の内側 2m でフェードアウトさせ、水域の縁で模様が急に切れる輪郭を防ぐ。
    float regionFade = 1.0f;
    if (gRegionValid != 0)
    {
        float2 regionDelta = abs(receiverWorldPos.xz - gRegionCenterXZ) - gRegionHalfExtentXZ;
        float outsideDistance = max(regionDelta.x, regionDelta.y);
        const float kRegionEdgeFadeMeters = 2.0f;
        regionFade = saturate(-outsideDistance / kRegionEdgeFadeMeters);
        if (regionFade <= 0.0f)
        {
            gCausticsOutput[launchIndex] = 0.0f.xxxx;
            return;
        }
    }

    float3 receiverNormal = normalize(gNormalRoughness.Load(int3(launchIndex, 0)).xyz * 2.0f - 1.0f);
    float3 waterPos = receiverWorldPos;
    waterPos.y = gWaterHeight;
    [unroll]
    for (int iteration = 0; iteration < 3; ++iteration)
    {
        float3 waveOffset = EvaluateCausticsWaterOffset(waterPos.xz);
        waterPos.y = gSurfaceWaterHeight + waveOffset.y;
    }

    float submergedDepth = waterPos.y - receiverWorldPos.y;
    if (submergedDepth <= 0.0f)
    {
        gCausticsOutput[launchIndex] = 0.0f.xxxx;
        return;
    }

    float shallowFade = saturate((submergedDepth - 0.03f) / 0.12f);
    shallowFade *= shallowFade;
    if (shallowFade <= 1.0e-4f)
    {
        gCausticsOutput[launchIndex] = 0.0f.xxxx;
        return;
    }

    float3 waterNormal = EvaluateCausticsWaterNormal(waterPos.xz);
    float3 lightDir = normalize(gLightDirection);
    // refract() の入射ベクトルは「光の進行方向」（=下向きの lightDir）を渡す。
    // 以前は -lightDir を渡しており、屈折方向の水平成分が反転して
    // コースティクスが本来と逆向き・不正な位置に集光していた。
    // （非RT 版 WaterCaustics.PS.hlsl も incidentDir = gMainLightDirection をそのまま使用している）
    float3 refractedDir = refract(lightDir, waterNormal, 1.0f / max(gRefractiveIndex, 1.001f));
    if (dot(refractedDir, refractedDir) <= 1.0e-6f || refractedDir.y >= -1.0e-4f)
    {
        gCausticsOutput[launchIndex] = 0.0f.xxxx;
        return;
    }

    RayDesc ray;
    ray.Origin = waterPos - waterNormal * gSurfaceBias;
    ray.Direction = normalize(refractedDir);
    ray.TMin = 0.001f;

    float3 rayToReceiver = receiverWorldPos - ray.Origin;
    float receiverRayT = dot(rayToReceiver, ray.Direction);
    if (receiverRayT <= 0.0f)
    {
        gCausticsOutput[launchIndex] = 0.0f.xxxx;
        return;
    }

    float3 projectedReceiverPos = ray.Origin + ray.Direction * receiverRayT;
    float receiverDistance = submergedDepth;
    float receiverRayDistance = length(projectedReceiverPos - receiverWorldPos);
    float receiverMatchRadius = max(0.03f, receiverDistance * 0.15f);
    if (receiverRayDistance > receiverMatchRadius)
    {
        gCausticsOutput[launchIndex] = 0.0f.xxxx;
        return;
    }

    ray.TMax = min(gMaxTraceDistance, receiverRayT + receiverMatchRadius * 2.0f);

    CausticsPayload payload;
    payload.hitT = 0.0f;
    payload.hitFlag = 0.0f;
    payload.ndotL = 0.0f;
    payload.receiverDepth = 0.0f;

    TraceRay(
        gScene,
        0,
        0xFF,
        0,
        1,
        0,
        ray,
        payload);

    if (payload.hitFlag < 0.5f)
    {
        gCausticsOutput[launchIndex] = 0.0f.xxxx;
        return;
    }

    float hitDistanceError = abs(payload.hitT - receiverRayT);
    float hitMatchRadius = max(0.10f, receiverMatchRadius * 2.0f);
    if (hitDistanceError > hitMatchRadius)
    {
        gCausticsOutput[launchIndex] = 0.0f.xxxx;
        return;
    }

    float rayMatchFactor = saturate(1.0f - receiverRayDistance / receiverMatchRadius);
    float hitMatchFactor = saturate(1.0f - hitDistanceError / hitMatchRadius);
    float matchFactor = rayMatchFactor * hitMatchFactor;
    matchFactor *= matchFactor;
    float receiverUpFactor = saturate(receiverNormal.y);
    float receiverFacingFactor = saturate(dot(receiverNormal, -ray.Direction));
    float attenuation = exp(-receiverDistance * 0.16f);
    float focus = saturate(dot(receiverNormal, -lightDir));
    // gLightIntensity をここで掛けることで、シーンのライト強度に連動させる
    // （以前は gIntensityScale のみで、ライトを暗くしても集光が弱まらなかった）。
    float intensity = gIntensityScale * gLightIntensity * attenuation * focus * matchFactor * shallowFade * receiverUpFactor * receiverFacingFactor * regionFade;

    if (gDebugViewMode != kRTCausticsDebugNone)
    {
        const float3 debugColor = BuildCausticsDebugColor(
            gDebugViewMode,
            shallowFade,
            matchFactor,
            attenuation,
            receiverFacingFactor,
            intensity);
        gCausticsOutput[launchIndex] = float4(debugColor, 1.0f);
        return;
    }

    // gLightColor を掛けることで、シーンのライト色（夕焼けなど）に連動させる
    // （以前は常にグレースケールの強度そのままで、ライト色を変えても白いままだった）。
    gCausticsOutput[launchIndex] = float4(gLightColor * intensity, 1.0f);
}

[shader("miss")]
void RTWaterCausticsMiss(inout CausticsPayload payload)
{
    payload.hitT = 0.0f;
    payload.hitFlag = 0.0f;
    payload.ndotL = 0.0f;
    payload.receiverDepth = 0.0f;
}

[shader("closesthit")]
void RTWaterCausticsClosestHit(
    inout CausticsPayload payload,
    in BuiltInTriangleIntersectionAttributes attr)
{
    payload.hitT = RayTCurrent();
    payload.hitFlag = 1.0f;
    payload.ndotL = 1.0f;
    payload.receiverDepth = RayTCurrent();
}
