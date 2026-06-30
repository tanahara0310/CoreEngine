struct SpectrumSample
{
    float2 h0;
    float2 h0Minus;
    float2 waveVector;
    float angularFrequency;
    float directionalWeight;
    float2 padding;
};

StructuredBuffer<SpectrumSample> gSpectrumSamples : register(t0);
RWTexture2D<float4> gHeightDisplacementXOutput : register(u0);
RWTexture2D<float4> gDisplacementZOutput : register(u1);

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

float2 ComplexMultiply(float2 a, float2 b)
{
    return float2(
        a.x * b.x - a.y * b.y,
        a.x * b.y + a.y * b.x);
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= gResolution || dispatchThreadId.y >= gResolution)
    {
        return;
    }

    const uint index = dispatchThreadId.y * gResolution + dispatchThreadId.x;
    SpectrumSample sample = gSpectrumSamples[index];

    const float angularPhase = sample.angularFrequency * gTimeSeconds;
    const float2 positiveRotation = float2(cos(angularPhase), sin(angularPhase));
    const float2 negativeRotation = float2(positiveRotation.x, -positiveRotation.y);

    float2 heightSpectrum = ComplexMultiply(sample.h0, positiveRotation)
        + ComplexMultiply(sample.h0Minus, negativeRotation);

    const float bandLimit = max((float)gActiveComponentCount / 64.0f, 1.0f / 64.0f);
    const float bandFade = saturate((bandLimit - sample.directionalWeight) * 16.0f + 1.0f);
    heightSpectrum *= bandFade * gAmplitudeScale;

    const float waveNumber = max(length(sample.waveVector), 1.0e-4f);
    const float2 direction = sample.waveVector / waveNumber;
    const float2 complexMinusIHeight = float2(heightSpectrum.y, -heightSpectrum.x);

    const float2 displacementXSpectrum = complexMinusIHeight * (direction.x * gChoppiness);
    const float2 displacementZSpectrum = complexMinusIHeight * (direction.y * gChoppiness);

    gHeightDisplacementXOutput[dispatchThreadId.xy] = float4(
        heightSpectrum.x,
        heightSpectrum.y,
        displacementXSpectrum.x,
        displacementXSpectrum.y);
    gDisplacementZOutput[dispatchThreadId.xy] = float4(
        displacementZSpectrum.x,
        displacementZSpectrum.y,
        0.0f,
        0.0f);
}
