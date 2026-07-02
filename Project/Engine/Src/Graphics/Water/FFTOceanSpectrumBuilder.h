#pragma once

#include <cstddef>
#include <cstdint>

namespace CoreEngine
{
    /// @brief FFT Ocean のスペクトル生成責務を担当するビルダー
    /// @details 設定のサニタイズ、Phillips Spectrum 生成、統計情報算出を分離して管理する。
    class FFTOceanSpectrumBuilder {
    public:
        struct Settings {
            uint32_t resolution = 256;
            float patchLength = 96.0f;
            float amplitudeScale = 1.0f;
            float windDirection[2] = { 0.92f, 0.38f };
            float windSpeed = 24.0f;
            float choppiness = 1.35f;
            uint32_t activeComponentCount = 32;
            float gravity = 9.81f;
        };

        struct SpectrumSample {
            float h0[2] = {};
            float h0Minus[2] = {};
            float waveVector[2] = {};
            float angularFrequency = 0.0f;
            float directionalWeight = 0.0f;
            float padding[2] = {};
        };

        struct BuildStats {
            uint32_t activeSpectrumSampleCount = 0;
            float averageSpectralAmplitude = 0.0f;
            float maxSpectralAmplitude = 0.0f;
            float maxAngularFrequency = 0.0f;
            uint32_t probeIndex = 0;
            SpectrumSample probeSample{};
        };

        /// @brief FFT Ocean 設定値を有効範囲へ正規化する
        static void SanitizeSettings(Settings& settings, uint32_t maxSpectrumComponents);

        /// @brief 指定バッファへスペクトルサンプルを生成する
        /// @param settings 生成設定
        /// @param outSpectrumSamples 出力先
        /// @param outSampleCount 出力バッファ要素数
        /// @return 生成統計情報
        static BuildStats BuildSpectrum(
            const Settings& settings,
            SpectrumSample* outSpectrumSamples,
            size_t outSampleCount);
    };
}
