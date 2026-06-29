struct SpectrumComponent
{
    float2 waveVector;
    float amplitude;
    float angularFrequency;
    float phaseOffset;
    float3 padding;
};

StructuredBuffer<SpectrumComponent> gSpectrumComponents : register(t0);
RWTexture2D<float4> gDisplacementOutput : register(u0);
RWTexture2D<float4> gNormalOutput : register(u1);

cbuffer FFTOceanSimulationConstants : register(b0)
{
    uint gResolution;
    uint gActiveComponentCount;
    float gPatchLength;
    float gTimeSeconds;
    float gAmplitudeScale;
    float gChoppiness;
    float2 gPadding;
};

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= gResolution || dispatchThreadId.y >= gResolution)
    {
        return;
    }

    float2 uv = (float2(dispatchThreadId.xy) + 0.5f) / max(float(gResolution), 1.0f);
    float2 centeredUV = uv * 2.0f - 1.0f;
    float2 worldXZ = centeredUV * (gPatchLength * 0.5f);

    float height = 0.0f;
    float2 horizontalDisplacement = 0.0f.xx;
    float2 slopes = 0.0f.xx;

    [loop]
    for (uint componentIndex = 0; componentIndex < 64; ++componentIndex)
    {
        if (componentIndex >= gActiveComponentCount)
        {
            break;
        }

        SpectrumComponent component = gSpectrumComponents[componentIndex];
        float2 waveVector = component.waveVector;
        float waveNumber = max(length(waveVector), 1.0e-4f);
        float phase = dot(waveVector, worldXZ) + component.angularFrequency * gTimeSeconds + component.phaseOffset;
        float sinPhase = sin(phase);
        float cosPhase = cos(phase);
        float amplitude = component.amplitude * gAmplitudeScale;
        float2 direction = waveVector / waveNumber;

        height += amplitude * sinPhase;
        horizontalDisplacement += -direction * (gChoppiness * amplitude * cosPhase);
        slopes += amplitude * waveVector * cosPhase;
    }

    float3 displacement = float3(horizontalDisplacement.x, height, horizontalDisplacement.y);
    float3 normal = normalize(float3(-slopes.x, 1.0f, -slopes.y));

    gDisplacementOutput[dispatchThreadId.xy] = float4(displacement, 1.0f);
    gNormalOutput[dispatchThreadId.xy] = float4(normal * 0.5f + 0.5f, 1.0f);
}
