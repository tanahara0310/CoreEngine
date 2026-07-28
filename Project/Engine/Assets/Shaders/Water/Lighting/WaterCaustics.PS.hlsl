#include "FullScreen.hlsli"
#include "../Include/Lighting/LightStructures.hlsli"
#include "../Include/Common/DepthReconstruction.hlsli"
#include "../Common/GerstnerWave.hlsli"

Texture2D<float> gSceneDepth : register(t0); // WorldPosition ターゲット廃止に伴い深度から復元する
Texture2D<float4> gNormalRoughness : register(t1);

cbuffer gMainLight : register(b1)
{
    float3 gMainLightColor;
    float gMainLightIntensity;
    float3 gMainLightDirection;
    uint gMainLightEnabled;
};

cbuffer gWaterSurfaceData : register(b2)
{
    float gSurfaceWaterHeight;
    uint gSurfaceActiveWaveCount;
    float gSurfaceTime;
    float gSurfacePadding;
    GerstnerWave gSurfaceWaves[GERSTNER_MAX_WAVE_COUNT];
    // 水面メッシュのワールドXZ範囲（WaterCausticsTechnique::WaterSurfaceConstants と一致）。
    // コースティクスは解析的な無限水面として評価されるため、この矩形でマスクしないと
    // 水域の外（無限床など「水面高さより低い場所すべて」）にも集光模様が漏れる。
    float2 gSurfaceRegionCenter;
    float2 gSurfaceRegionHalfExtent;
    uint gSurfaceRegionValid;
    float3 gSurfaceRegionPadding;
};

// C++ 側 WaterCausticsTechnique::Params と厳密にレイアウトを一致させること。
// gPadding0 までの8フィールド(32B)はC++側の
// intensity/depthAttenuation/curvatureScale/surfaceSampleRadius/refractiveIndex/
// receiverNormalStrength/alignmentPower/debugDisplayScale に対応（このシェーダーは未使用）。
// 続く16B（debugViewMode/debugLogEnabled/padding[2]）は本シェーダーで読まないため
// gUnusedTail で読み飛ばし、その後ろの invViewProjMatrix[16] を gInvViewProj として受け取る。
cbuffer WaterCausticsParams : register(b3)
{
    float gIntensity;
    float gDepthAttenuation;
    float gCurvatureScale;
    float gSurfaceSampleRadius;
    float gRefractiveIndex;
    float gReceiverNormalStrength;
    float gAlignmentPower;
    float gPadding0;
    float4 gUnusedTail; // C++側 debugViewMode/debugLogEnabled/padding[2] (16B)
    float4x4 gInvViewProj;
};

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

struct PixelShaderOutput
{
    float4 color : SV_Target;
};

// 波の数式は Common/GerstnerWave.hlsli が唯一の情報源（DXR 側の
// RTWaterSurfaceCommon.hlsli と同じ関数を使う）。cbuffer レイアウトだけが
// スクリーンスペース版と DXR 版で異なるため、ループはここに残す。
float3 EvaluateWaterOffset(float2 worldXZ)
{
    float3 totalOffset = 0.0f.xxx;

    [unroll]
    for (uint waveIndex = 0; waveIndex < kMaxGerstnerWaveCount; ++waveIndex)
    {
        if (waveIndex >= gSurfaceActiveWaveCount)
        {
            break;
        }

        totalOffset += EvaluateGerstnerWaveOffset(gSurfaceWaves[waveIndex], gSurfaceTime, worldXZ);
    }

    return totalOffset;
}

float3 EvaluateWaterNormal(float2 worldXZ)
{
    float3 dPdX = float3(1.0f, 0.0f, 0.0f);
    float3 dPdZ = float3(0.0f, 0.0f, 1.0f);

    [unroll]
    for (uint waveIndex = 0; waveIndex < kMaxGerstnerWaveCount; ++waveIndex)
    {
        if (waveIndex >= gSurfaceActiveWaveCount)
        {
            break;
        }

        AccumulateGerstnerWaveDerivatives(
            gSurfaceWaves[waveIndex], gSurfaceTime, worldXZ, dPdX, dPdZ);
    }

    return BuildGerstnerNormal(dPdX, dPdZ);
}

float EvaluateCurvature(float2 worldXZ)
{
    float radius = max(gSurfaceSampleRadius, 0.05f);
    float3 nxp = EvaluateWaterNormal(worldXZ + float2(radius, 0.0f));
    float3 nxm = EvaluateWaterNormal(worldXZ + float2(-radius, 0.0f));
    float3 nzp = EvaluateWaterNormal(worldXZ + float2(0.0f, radius));
    float3 nzm = EvaluateWaterNormal(worldXZ + float2(0.0f, -radius));
    return length(nxp - nxm) + length(nzp - nzm);
}

float2 TraceRefractedHitXZ(float2 surfaceXZ, float receiverY, out bool valid)
{
    float3 surfaceOffset = EvaluateWaterOffset(surfaceXZ);
    float3 waterNormal = EvaluateWaterNormal(surfaceXZ);
    float3 incidentDir = normalize(gMainLightDirection);
    float eta = 1.0f / max(gRefractiveIndex, 1.001f);
    float3 refractedDir = refract(incidentDir, waterNormal, eta);

    valid = false;
    if (dot(refractedDir, refractedDir) <= 1.0e-6f) {
        return 0.0f.xx;
    }

    refractedDir = normalize(refractedDir);
    if (refractedDir.y >= -1.0e-4f) {
        return 0.0f.xx;
    }

    float surfaceY = gSurfaceWaterHeight + surfaceOffset.y;
    float travelT = (receiverY - surfaceY) / refractedDir.y;
    if (travelT <= 0.0f) {
        return 0.0f.xx;
    }

    valid = true;
    return surfaceXZ + surfaceOffset.xz + refractedDir.xz * travelT;
}

PixelShaderOutput main(PixelShaderInput input)
{
    PixelShaderOutput output;
    output.color = float4(0.0f, 0.0f, 0.0f, 1.0f);

    int3 loadCoord = int3(input.position.xy, 0);
    float ndcDepth = gSceneDepth.Load(loadCoord);
    if (IsBackgroundDepth(ndcDepth) || gSurfaceActiveWaveCount == 0 || gMainLightEnabled == 0)
    {
        return output;
    }

    float depthW, depthH;
    gSceneDepth.GetDimensions(depthW, depthH);
    float2 screenUV = (input.position.xy + 0.5f.xx) / float2(depthW, depthH);
    float3 worldPos = ReconstructWorldPosition(ScreenUVToNDC(screenUV), ndcDepth, gInvViewProj);

    // 水面メッシュの XZ 範囲外（無限床など水域の外）にはコースティクスを落とさない。
    // 範囲の内側 2m でフェードアウトさせ、水域の縁で模様が急に切れる輪郭を防ぐ。
    float regionFade = 1.0f;
    if (gSurfaceRegionValid != 0)
    {
        float2 regionDelta = abs(worldPos.xz - gSurfaceRegionCenter) - gSurfaceRegionHalfExtent;
        float outsideDistance = max(regionDelta.x, regionDelta.y);
        const float kRegionEdgeFadeMeters = 2.0f;
        regionFade = saturate(-outsideDistance / kRegionEdgeFadeMeters);
        if (regionFade <= 0.0f)
        {
            return output;
        }
    }

    float3 surfaceOffset = EvaluateWaterOffset(worldPos.xz);
    float surfaceY = gSurfaceWaterHeight + surfaceOffset.y;
    float waterDepth = surfaceY - worldPos.y;
    if (waterDepth <= 0.0f)
    {
        return output;
    }

    float3 receiverNormal = normalize(gNormalRoughness.Load(loadCoord).rgb * 2.0f - 1.0f);
    float3 waterNormal = EvaluateWaterNormal(worldPos.xz);

    bool validCenter = false;
    bool validXp = false;
    bool validXm = false;
    bool validZp = false;
    bool validZm = false;
    float sampleStep = max(gSurfaceSampleRadius, 0.05f);
    float2 hitCenter = TraceRefractedHitXZ(worldPos.xz, worldPos.y, validCenter);
    float2 hitXp = TraceRefractedHitXZ(worldPos.xz + float2(sampleStep, 0.0f), worldPos.y, validXp);
    float2 hitXm = TraceRefractedHitXZ(worldPos.xz + float2(-sampleStep, 0.0f), worldPos.y, validXm);
    float2 hitZp = TraceRefractedHitXZ(worldPos.xz + float2(0.0f, sampleStep), worldPos.y, validZp);
    float2 hitZm = TraceRefractedHitXZ(worldPos.xz + float2(0.0f, -sampleStep), worldPos.y, validZm);
    if (!(validCenter && validXp && validXm && validZp && validZm))
    {
        return output;
    }

    float2 dx = (hitXp - hitXm) / max(2.0f * sampleStep, 1.0e-4f);
    float2 dz = (hitZp - hitZm) / max(2.0f * sampleStep, 1.0e-4f);
    float jacobian = abs(dx.x * dz.y - dx.y * dz.x);
    if (jacobian <= 1.0e-5f) {
        return output;
    }

    float2 focusDelta = hitCenter - worldPos.xz;
    float focusRadius = max(gSurfaceSampleRadius * max(gAlignmentPower, 1.0f), 0.25f);
    float proximity = saturate(1.0f - length(focusDelta) / focusRadius);
    proximity *= proximity;

    float3 incidentDir = normalize(gMainLightDirection);
    float eta = 1.0f / max(gRefractiveIndex, 1.001f);
    float3 refractedDir = normalize(refract(incidentDir, waterNormal, eta));
    float receiverFacing = saturate(dot(receiverNormal, -refractedDir) * max(gReceiverNormalStrength, 0.0f));
    float curvature = 1.0f + saturate(EvaluateCurvature(worldPos.xz) * gCurvatureScale);
    float attenuation = exp(-waterDepth * max(gDepthAttenuation, 0.01f));
    float concentration = rcp(max(jacobian, 0.05f));

    float strength = gIntensity * gMainLightIntensity * proximity * concentration * curvature * receiverFacing * attenuation * regionFade;
    strength = strength / (1.0f + strength);
    output.color = float4(gMainLightColor * strength, 1.0f);
    return output;
}
