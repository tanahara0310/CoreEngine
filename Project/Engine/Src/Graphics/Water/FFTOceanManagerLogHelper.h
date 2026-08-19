#pragma once

#include <cstdint>

namespace CoreEngine
{
    /// @brief FFTOceanManager の診断ログ出力を担当するヘルパー
    class FFTOceanManagerLogHelper {
    public:
        /// @brief 初期化時の解像度と風速をログへ出す
        static void LogInitialize(uint32_t resolution, float windSpeed);
        static void LogSettingsUpdated(
            float amplitudeScale,
            float windDirX,
            float windDirY,
            float windSpeed,
            float choppiness,
            uint32_t activeComponentCount,
            float gravity);
        static void LogDispatchSummary(
            float timeSeconds,
            float deltaSeconds,
            float cbTimeSeconds,
            float amplitudeScale,
            float windSpeed,
            float choppiness,
            uint32_t activeComponentCount);
        static void LogProbeSpectrum(
            uint32_t probeIndex,
            float waveVectorX,
            float waveVectorY,
            float angularFrequency,
            float real,
            float imag,
            float magnitude,
            float directionalWeight);
        static void LogIFFTCompleted(
            uint32_t finalSpectrumAIndex,
            uint32_t finalSpectrumBIndex,
            uint32_t resolution,
            uint32_t log2Resolution);
        /// @brief スペクトルのアップロード量をログへ出す
        static void LogSpectrumUpload(uint64_t uploadBytes);
    };
}
