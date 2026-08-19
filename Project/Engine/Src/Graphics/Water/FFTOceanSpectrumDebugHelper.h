#pragma once

#include <cstdint>

namespace CoreEngine
{
    /// @brief FFT Spectrum のデバッグ評価を担当するヘルパー
    class FFTOceanSpectrumDebugHelper {
    public:
        /// @brief 複素数（デバッグ表示用）
        struct ComplexValue {
            float real = 0.0f;
            float imag = 0.0f;
        };

        /// @brief H0/H0* と角周波数から時刻 t のスペクトル値を評価する
        static ComplexValue EvaluateSpectrumSample(
            const float* h0,
            const float* h0Minus,
            float angularFrequency,
            float directionalWeight,
            float timeSeconds,
            float amplitudeScale,
            uint32_t activeComponentCount);

        /// @brief 複素数の大きさを返す
        static float ComputeMagnitude(const ComplexValue& value);
    };
}
