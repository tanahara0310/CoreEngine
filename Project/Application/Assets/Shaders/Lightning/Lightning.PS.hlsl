#include "Object3dForward.hlsli"

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

struct LightningPSInput
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

PixelShaderOutput main(LightningPSInput input)
{
    PixelShaderOutput output;

    const float duration = max(gDuration, 1.0e-4f);
    const float age01 = saturate(gElapsedTime / duration);
    const float life = gActive * pow(saturate(1.0f - age01), max(gFadePower, 0.1f));
    const float revealPhase = saturate(age01 / 0.16f);
    const float revealFront = revealPhase;
    const float revealSoftness = 0.035f;
    const float revealMask = 1.0f - smoothstep(revealFront - revealSoftness, revealFront + revealSoftness, input.texcoord.y);
    const float revealFrontGlow = exp(-abs(input.texcoord.y - revealFront) * 42.0f) * saturate(1.0f - age01 * 1.8f);

    const float edge = abs(input.texcoord.x * 2.0f - 1.0f);
    const float coreMask = pow(saturate(1.0f - edge), max(gCoreExponent, 0.1f));
    const float haloMask = pow(saturate(1.0f - edge), max(gHaloExponent, 0.1f));
    const float along = saturate(1.0f - input.texcoord.y);
    const float tipBoost = pow(along, 0.18f);
    const float filamentMask = saturate((abs(input.worldPosition.x) + abs(input.worldPosition.z)) * 1.9f);

    float3 noisePosition = input.worldPosition * (gNoiseTiling * lerp(0.9f, 1.14f, gShapeSeed.x))
        + float3(
            gShapeSeed.z * 13.1f,
            -gElapsedTime * (gNoiseScrollSpeed * lerp(0.88f, 1.18f, gShapeSeed.y)),
            gElapsedTime * (0.45f + gShapeSeed.w * 0.55f) + gShapeSeed.x * 9.7f);
    float noiseA = Noise3D(noisePosition);
    float noiseB = Noise3D(noisePosition * lerp(1.9f, 2.8f, gShapeSeed.z) + 9.1f + gShapeSeed.y * 7.0f);
    float longitudinalPulse = 0.5f + 0.5f * sin((1.0f - along) * lerp(31.0f, 54.0f, gShapeSeed.w) - gElapsedTime * lerp(70.0f, 104.0f, gShapeSeed.x) + noiseB * 6.28318f);
    float flicker = saturate(0.52f + noiseA * 0.30f + longitudinalPulse * 0.30f + sin(gElapsedTime * lerp(98.0f, 142.0f, gShapeSeed.z) + input.worldPosition.y * (11.0f + gShapeSeed.y * 8.0f) + gShapeSeed.w * 5.0f) * 0.14f);

    float flash = lerp(1.4f, 0.78f, age01);
    float emissive = gIntensity * life * flash * flicker;
    emissive *= 1.0f + filamentMask * gBranchPulse * 0.12f;
    emissive *= revealMask;
    emissive *= 1.0f + revealFrontGlow * 1.8f;

    float3 haloColor = gMaterial.color.rgb * float3(0.68f, 0.80f, 1.00f);
    float3 coreColor = max(gMaterial.color.rgb, float3(1.05f, 1.05f, 1.08f));
    float3 color = haloColor * haloMask * emissive * 1.45f;
    color += coreColor * coreMask * emissive * (4.6f + tipBoost * 1.2f);
    color += float3(0.10f, 0.16f, 0.30f) * haloMask * noiseB * emissive;

    output.color.rgb = color;
    output.color.a = saturate(max(coreMask, haloMask) * life * gMaterial.color.a * revealMask);
    return output;
}
