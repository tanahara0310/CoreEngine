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

            // ガウス乱数のシード。カスケードごとに必ず変えること。
            // 全カスケードが同一シードだと位相パターンが完全相関し、
            // 「同じ波の配置が縮尺違いで全域に繰り返される」自己相似アーティファクトになる
            // （2026-07-21 に実際に発生した既知バグ）。
            uint32_t randomSeed = 20260626u;

            // 生成後の波高RMS（メートル）をこの値へ正規化する（0 以下で正規化なし）。
            // 素の Phillips スペクトルは離散化の Δk 正規化を持たず、波高が
            // patchLength × windSpeed² にほぼ比例して無制限に成長する
            // （L=340m/風速24m/s で RMS≈20m の「水の山」になる既知バグ）。
            // 呼び出し側が物理的に妥当な目標RMS（例: Pierson-Moskowitz の Hs/4）を
            // 渡すことで、パッチ長に依存しない現実的な波高に較正する。
            float targetRmsHeight = 0.0f;
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

            // 正規化前の推定波高RMS（m）と、targetRmsHeight 適用時のスケール係数。
            float measuredRmsHeight = 0.0f;
            float appliedHeightScale = 1.0f;
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
