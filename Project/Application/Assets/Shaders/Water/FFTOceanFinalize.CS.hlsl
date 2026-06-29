Texture2D<float4> gHeightDisplacementXSpectrum : register(t0);
Texture2D<float4> gDisplacementZSpectrum : register(t1);
RWTexture2D<float4> gDisplacementOutput : register(u0);
RWTexture2D<float4> gNormalOutput : register(u1);

cbuffer FFTOceanSimulationConstants : register(b0)
{
    uint gResolution;
    uint gActiveComponentCount;
    float gPatchLength;
    float gTimeSeconds;
    float gChoppiness;
    float gGravity;
    float gAmplitudeScale;
    float gPadding;
};

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= gResolution || dispatchThreadId.y >= gResolution)
    {
        return;
    }

    const uint2 coord = dispatchThreadId.xy;
    const uint2 coordLeft = uint2((coord.x + gResolution - 1u) % gResolution, coord.y);
    const uint2 coordRight = uint2((coord.x + 1u) % gResolution, coord.y);
    const uint2 coordDown = uint2(coord.x, (coord.y + gResolution - 1u) % gResolution);
    const uint2 coordUp = uint2(coord.x, (coord.y + 1u) % gResolution);

    const float4 spectrumA = gHeightDisplacementXSpectrum.Load(int3(coord, 0));
    const float4 spectrumB = gDisplacementZSpectrum.Load(int3(coord, 0));

    const float checker = ((coord.x + coord.y) & 1u) != 0u ? -1.0f : 1.0f;

    const float debugScale = 1.0f;
    const float displacementX = spectrumA.z * checker * debugScale;
    const float height = spectrumA.x * checker * debugScale;
    const float displacementZ = spectrumB.x * checker * debugScale;

    const float leftHeight = gHeightDisplacementXSpectrum.Load(int3(coordLeft, 0)).x * ((((coordLeft.x + coordLeft.y) & 1u) != 0u) ? -1.0f : 1.0f);
    const float rightHeight = gHeightDisplacementXSpectrum.Load(int3(coordRight, 0)).x * ((((coordRight.x + coordRight.y) & 1u) != 0u) ? -1.0f : 1.0f);
    const float downHeight = gHeightDisplacementXSpectrum.Load(int3(coordDown, 0)).x * ((((coordDown.x + coordDown.y) & 1u) != 0u) ? -1.0f : 1.0f);
    const float upHeight = gHeightDisplacementXSpectrum.Load(int3(coordUp, 0)).x * ((((coordUp.x + coordUp.y) & 1u) != 0u) ? -1.0f : 1.0f);

    const float texelLength = gPatchLength / (float)gResolution;
    const float slopeX = (rightHeight - leftHeight) / max(2.0f * texelLength, 1.0e-4f);
    const float slopeZ = (upHeight - downHeight) / max(2.0f * texelLength, 1.0e-4f);
    const float3 normal = normalize(float3(-slopeX, 1.0f, -slopeZ));

    gDisplacementOutput[coord] = float4(displacementX, height, displacementZ, 1.0f);
    gNormalOutput[coord] = float4(normalize(normal) * 0.5f + 0.5f, 1.0f);
}
