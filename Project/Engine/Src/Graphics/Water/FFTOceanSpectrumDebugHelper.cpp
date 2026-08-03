#include "pch.h"
#include "FFTOceanSpectrumDebugHelper.h"

#include <algorithm>
#include <cmath>

namespace CoreEngine
{
    FFTOceanSpectrumDebugHelper::ComplexValue FFTOceanSpectrumDebugHelper::EvaluateSpectrumSample(
        const float* h0,
        const float* h0Minus,
        float angularFrequency,
        float directionalWeight,
        float timeSeconds,
        float amplitudeScale,
        uint32_t activeComponentCount)
    {
        ComplexValue value{};
        if (!h0 || !h0Minus) {
            return value;
        }

        const float angularPhase = angularFrequency * timeSeconds;
        const float cosPhase = std::cos(angularPhase);
        const float sinPhase = std::sin(angularPhase);

        // h0Minus 側は共役回転 e^{-iωt} を掛ける: 実部 = x·cos + y·sin
        // （FFTOceanTimeEvolution.CS.hlsl の negativeRotation と一致させること）
        value.real =
            (h0[0] * cosPhase - h0[1] * sinPhase)
            + (h0Minus[0] * cosPhase + h0Minus[1] * sinPhase);
        value.imag =
            (h0[0] * sinPhase + h0[1] * cosPhase)
            + (-h0Minus[0] * sinPhase + h0Minus[1] * cosPhase);

        const float bandLimit = (std::max)(static_cast<float>(activeComponentCount) / 64.0f, 1.0f / 64.0f);
        const float bandFade = (std::clamp)((bandLimit - directionalWeight) * 16.0f + 1.0f, 0.0f, 1.0f);
        value.real *= bandFade * amplitudeScale;
        value.imag *= bandFade * amplitudeScale;
        return value;
    }

    float FFTOceanSpectrumDebugHelper::ComputeMagnitude(const ComplexValue& value)
    {
        return std::sqrt(value.real * value.real + value.imag * value.imag);
    }
}
