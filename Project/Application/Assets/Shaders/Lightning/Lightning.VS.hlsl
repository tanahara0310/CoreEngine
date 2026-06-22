#include "Object3dVertex.hlsli"

cbuffer LightningConstants : register(b4)
{
    float gElapsedTime;
    float gDuration;
    float gIntensity;
    float gActive;

    float gNoiseTiling;
    float gNoiseScrollSpeed;
    float gCoreExponent;
    float gHaloExponent;

    float gFadePower;
    float gWidthScale;
    float gDistortionAmplitude;
    float gBranchPulse;

    float4 gShapeSeed;
};

struct LightningVSOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float3 worldPosition : POSITION0;
    float4 lightSpacePos : POSITION1;
    float3 tangent : TANGENT0;
    float3 bitangent : BINORMAL0;
    float4 clipPosCurrent : POSITION2;
    float4 clipPosPrev : POSITION3;
};

float Hash31(float3 p)
{
    p = frac(p * 0.1031f);
    p += dot(p, p.yzx + 33.33f);
    return frac((p.x + p.y) * p.z);
}

float Noise3D(float3 p)
{
    float3 i = floor(p);
    float3 f = frac(p);
    f = f * f * (3.0f - 2.0f * f);

    float n000 = Hash31(i + float3(0.0f, 0.0f, 0.0f));
    float n100 = Hash31(i + float3(1.0f, 0.0f, 0.0f));
    float n010 = Hash31(i + float3(0.0f, 1.0f, 0.0f));
    float n110 = Hash31(i + float3(1.0f, 1.0f, 0.0f));
    float n001 = Hash31(i + float3(0.0f, 0.0f, 1.0f));
    float n101 = Hash31(i + float3(1.0f, 0.0f, 1.0f));
    float n011 = Hash31(i + float3(0.0f, 1.0f, 1.0f));
    float n111 = Hash31(i + float3(1.0f, 1.0f, 1.0f));

    float nx00 = lerp(n000, n100, f.x);
    float nx10 = lerp(n010, n110, f.x);
    float nx01 = lerp(n001, n101, f.x);
    float nx11 = lerp(n011, n111, f.x);
    float nxy0 = lerp(nx00, nx10, f.y);
    float nxy1 = lerp(nx01, nx11, f.y);
    return lerp(nxy0, nxy1, f.z);
}

float3 ComputeLightningOffsetWorld(
    float3 worldPos,
    float3 worldNormal,
    float3 worldBitangent,
    float2 texcoord,
    float3 localPosition,
    float elapsedTime,
    float life)
{
    const float edge = abs(texcoord.x * 2.0f - 1.0f);
    const float centerWeight = saturate(1.0f - edge);
    const float filamentWeight = saturate(abs(localPosition.x) + abs(localPosition.z)) * 2.2f;
    const float along = saturate(-localPosition.y);

    float tiling = gNoiseTiling * lerp(0.82f, 1.18f, gShapeSeed.y);
    float3 noisePosition = worldPos * tiling
        + float3(
            gShapeSeed.x * 19.3f,
            -elapsedTime * (gNoiseScrollSpeed * lerp(0.9f, 1.15f, gShapeSeed.z)) + gShapeSeed.y * 23.7f,
            elapsedTime * (0.35f + gShapeSeed.w * 0.65f) + gShapeSeed.z * 17.1f);
    float noiseA = Noise3D(noisePosition);
    float noiseB = Noise3D(noisePosition * lerp(1.55f, 2.35f, gShapeSeed.z) + 13.7f + gShapeSeed.x * 11.0f);
    float primaryBend = sin(along * lerp(18.0f, 34.0f, gShapeSeed.x) - elapsedTime * lerp(20.0f, 34.0f, gShapeSeed.y) + gShapeSeed.z * 6.28318f + noiseB * 1.7f);
    float secondaryBend = sin(along * lerp(8.0f, 16.0f, gShapeSeed.w) + elapsedTime * lerp(11.0f, 21.0f, gShapeSeed.x) + gShapeSeed.y * 9.0f);
    float sway = sin(worldPos.y * lerp(22.0f, 38.0f, gShapeSeed.z) - elapsedTime * lerp(42.0f, 76.0f, gShapeSeed.w) + noiseB * 6.28318f);
    float zigzag = primaryBend * 0.55f + secondaryBend * 0.25f + sway * 0.20f;
    float distortion = (noiseA * 2.0f - 1.0f) * 0.38f + zigzag * 0.62f;
    distortion *= gDistortionAmplitude * gWidthScale * life * (0.28f + centerWeight * 0.72f) * (1.0f + filamentWeight * gBranchPulse * 0.18f);

    return normalize(worldNormal + worldBitangent * lerp(0.12f, 0.32f, gShapeSeed.y)) * distortion;
}

LightningVSOutput main(VertexShaderInput input, uint instanceID : SV_InstanceID)
{
    TransformationMatrix mtx = gInstanceData[instanceID];

    const float duration = max(gDuration, 1.0e-4f);
    const float age01Current = saturate(gElapsedTime / duration);
    const float lifeCurrent = gActive * pow(saturate(1.0f - age01Current), max(gFadePower, 0.1f));
    const float prevElapsedTime = max(gElapsedTime - (1.0f / 60.0f), 0.0f);
    const float age01Prev = saturate(prevElapsedTime / duration);
    const float lifePrev = gActive * pow(saturate(1.0f - age01Prev), max(gFadePower, 0.1f));

    float4 baseClip = mul(input.position, mtx.WVP);
    float3 worldPos = mul(input.position, mtx.World).xyz;
    float3 worldNormal = normalize(mul(input.normal, (float3x3)mtx.WorldInversTranspose));
    float3 worldTangent = normalize(mul(input.tangent, (float3x3)mtx.World));
    float3 worldBitangent = normalize(cross(worldNormal, worldTangent));

    float3 offsetWorld = ComputeLightningOffsetWorld(
        worldPos,
        worldNormal,
        worldBitangent,
        input.texcoord,
        input.position.xyz,
        gElapsedTime,
        lifeCurrent);
    worldPos += offsetWorld;

    float3x3 invWorld3 = transpose((float3x3)mtx.WorldInversTranspose);
    float3 offsetLocal = mul(offsetWorld, invWorld3);
    float4 offsetClip = mul(float4(offsetLocal, 0.0f), mtx.WVP);

    float3 prevOffsetWorld = ComputeLightningOffsetWorld(
        mul(input.position, mtx.World).xyz,
        worldNormal,
        worldBitangent,
        input.texcoord,
        input.position.xyz,
        prevElapsedTime,
        lifePrev);
    float3 prevOffsetLocal = mul(prevOffsetWorld, invWorld3);
    float4 prevOffsetClip = mul(float4(prevOffsetLocal, 0.0f), mtx.PrevWVP);

    LightningVSOutput output;
    output.position = baseClip + offsetClip;
    output.texcoord = input.texcoord;
    output.normal = worldNormal;
    output.worldPosition = worldPos;
    output.lightSpacePos = mul(float4(worldPos, 1.0f), mtx.LightViewProjection);
    output.tangent = worldTangent;
    output.bitangent = worldBitangent;
    output.clipPosCurrent = output.position;
    output.clipPosPrev = mul(input.position, mtx.PrevWVP) + prevOffsetClip;
    return output;
}
