Texture2D<float4> gInputSpectrum : register(t0);
RWTexture2D<float4> gOutputSpectrum : register(u0);

cbuffer FFTOceanIFFTConstants : register(b0)
{
    uint gResolution;
    uint gStageIndex;
    uint gIsHorizontal;
    float gNormalizationScale;
};

static const float PI = 3.14159265359f;

uint ReverseIndex(uint index, uint resolution)
{
    const uint bitCount = firstbithigh(resolution);
    return reversebits(index) >> (32u - bitCount);
}

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

    const uint stride = 1u << gStageIndex;
    const uint butterflySpan = stride << 1u;

    uint lineIndex = gIsHorizontal != 0u ? dispatchThreadId.y : dispatchThreadId.x;
    uint elementIndex = gIsHorizontal != 0u ? dispatchThreadId.x : dispatchThreadId.y;

    const uint blockBase = (elementIndex / butterflySpan) * butterflySpan;
    const uint pairOffset = elementIndex % stride;
    const uint firstIndex = blockBase + pairOffset;
    const uint secondIndex = firstIndex + stride;

    uint sourceFirstIndex = firstIndex;
    uint sourceSecondIndex = secondIndex;
    if (gStageIndex == 0u)
    {
        sourceFirstIndex = ReverseIndex(firstIndex, gResolution);
        sourceSecondIndex = ReverseIndex(secondIndex, gResolution);
    }

    uint2 firstCoord = gIsHorizontal != 0u ? uint2(sourceFirstIndex, lineIndex) : uint2(lineIndex, sourceFirstIndex);
    uint2 secondCoord = gIsHorizontal != 0u ? uint2(sourceSecondIndex, lineIndex) : uint2(lineIndex, sourceSecondIndex);

    float4 firstValue = gInputSpectrum.Load(int3(firstCoord, 0));
    float4 secondValue = gInputSpectrum.Load(int3(secondCoord, 0));

    const float angle = 2.0f * PI * (float)pairOffset / (float)butterflySpan;
    const float2 twiddle = float2(cos(angle), sin(angle));
    const float2 twiddledXY = ComplexMultiply(secondValue.xy, twiddle);
    const float2 twiddledZW = ComplexMultiply(secondValue.zw, twiddle);

    float4 outputValue;
    if ((elementIndex % butterflySpan) < stride)
    {
        outputValue.xy = firstValue.xy + twiddledXY;
        outputValue.zw = firstValue.zw + twiddledZW;
    }
    else
    {
        outputValue.xy = firstValue.xy - twiddledXY;
        outputValue.zw = firstValue.zw - twiddledZW;
    }

    outputValue *= gNormalizationScale;
    gOutputSpectrum[dispatchThreadId.xy] = outputValue;
}
