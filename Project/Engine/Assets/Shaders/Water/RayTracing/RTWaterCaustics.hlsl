// ============================================================
// DXR 水面コースティクスシェーダー
// 第2段階ではまず受光点ごとの簡易集光量を専用RT出力へ書き出す。
// ============================================================

#include "RTWaterSurfaceCommon.hlsli"

RWTexture2D<float4> gCausticsOutput : register(u0);
RaytracingAccelerationStructure gScene : register(t0);
Texture2D<float4> gWorldPosition : register(t1);
Texture2D<float4> gNormalRoughness : register(t2);

cbuffer WaterCausticsConstants : register(b0)
{
    float gMaxTraceDistance;
    float gSurfaceBias;
    float gIntensityScale;
    float gWaterHeight;
    float3 gLightDirection;
    float gScreenWidth;
    float gScreenHeight;
    float3 gPadding;
};

struct CausticsPayload
{
    float hitT;
    float hitFlag;
    float ndotL;
    float receiverDepth;
};

[shader("raygeneration")]
void RTWaterCausticsRayGen()
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    float4 worldPosSample = gWorldPosition.Load(int3(launchIndex, 0));
    if (worldPosSample.a < 0.5f)
    {
        gCausticsOutput[launchIndex] = 0.0f.xxxx;
        return;
    }

    float3 receiverWorldPos = worldPosSample.xyz;
    float3 receiverNormal = normalize(gNormalRoughness.Load(int3(launchIndex, 0)).xyz * 2.0f - 1.0f);
    float3 waterPos = receiverWorldPos;
    waterPos.y = gWaterHeight;
    [unroll]
    for (int iteration = 0; iteration < 3; ++iteration)
    {
        float3 waveOffset = EvaluateWaterOffset(waterPos.xz);
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

    float3 waterNormal = EvaluateWaterNormal(waterPos.xz);
    float3 lightDir = normalize(gLightDirection);
    float3 refractedDir = refract(-lightDir, waterNormal, 1.0f / 1.333f);
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
    receiverUpFactor *= receiverUpFactor;
    float receiverFacingFactor = saturate(dot(receiverNormal, -ray.Direction));
    receiverFacingFactor *= receiverFacingFactor;
    float attenuation = exp(-receiverDistance * 0.35f);
    float focus = saturate(dot(receiverNormal, -lightDir));
    float intensity = saturate(gIntensityScale * attenuation * focus * matchFactor * shallowFade * receiverUpFactor * receiverFacingFactor);
    gCausticsOutput[launchIndex] = float4(intensity.xxx, 1.0f);
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
