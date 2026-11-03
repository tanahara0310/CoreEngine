#pragma once

#include "Graphics/Water/FFT/FFTOceanSpectrumBuilder.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace CoreEngine
{
    /// @brief FFT の海のスペクトル（設定・カスケードごとの初期スペクトルの生成と波高の較正・波の統計）
    /// @details GPU は使わない。初期スペクトルは呼び出し側が渡したバッファへ書く
    class FFTOceanSpectrum
    {
    public:
        /// @brief カスケードの数（各カスケードはパッチ長の違う独立した FFT）
        /// @details パッチ長・格子の回転・帯域の境界は FFTOceanCascadeValues.hlsli が持つ
        static constexpr uint32_t kCascadeCount = 3;

        /// @brief スペクトルの成分の数の上限
        static constexpr uint32_t kMaxComponents = 64;

        using SpectrumSample = FFTOceanSpectrumBuilder::SpectrumSample;

        /// @brief スペクトルの設定
        struct Settings {
            uint32_t resolution = 256;
            float amplitudeScale = 1.0f;
            float windDirection[2] = { 0.92f, 0.38f };
            // 波高とピーク波長は JONSWAP のフェッチ制限成長則で「風速 × 吹送距離」から
            // 決まる（完全発達 = Pierson-Moskowitz でクランプ）。
            float windSpeed = 12.0f;
            /// @brief 吹送距離 [m]。短い＝若く尖った風波、長い＝完全発達したうねり
            float fetchMeters = 200000.0f;
            float choppiness = 1.35f;
            uint32_t activeComponentCount = 32;
            float gravity = 9.81f;

            // ---- うねり（swell）成分 ----
            // 遠方の低気圧で発生し、分散しながら到達した長周期波。その場の風とは
            // 独立なので、風速ではなく波高・周期・向きで直接与える。
            // 風波と別方向にすると斜交する波列（クロスシー）になり、風速を落としても
            // うねりだけが残る（＝凪の海のうねり）。
            bool swellEnabled = true;
            float swellHeightMeters = 1.2f;    ///< うねりの有義波高 Hs [m]
            float swellPeriodSeconds = 12.0f;  ///< うねりのピーク周期 [s]（λ = gT²/2π）
            float swellDirection[2] = { 0.34f, -0.94f }; ///< 進行方向（XZ・適用時に正規化）
            float swellRelativeWidth = 0.06f;  ///< 周波数の狭さ（小さいほど周期が揃う）
            float swellSpreadExponent = 24.0f; ///< 指向の鋭さ（大きいほど一方向）
        };

        /// @brief スペクトルの波の大きさ（風波とうねりのピーク波長と有義波高）
        struct WaveSummary {
            float windSeaWavelength = 0.0f; ///< 風波のピーク波長 [m]
            float windSeaHeight = 0.0f;     ///< 風波の有義波高 Hs [m]
            float swellWavelength = 0.0f;   ///< うねりのピーク波長 [m]（うねりが無ければ 0）
            float swellHeight = 0.0f;       ///< うねりの有義波高 Hs [m]（うねりが無ければ 0）
        };

        /// @brief 既定の設定を有効範囲へ整えて持つ
        FFTOceanSpectrum();

        /// @brief 設定を有効範囲へ整えて持つ（解像度は今の値のまま）
        /// @return 整えた設定が今の設定と違ったか（同じなら何もしない）
        bool UpdateSettings(const Settings& settings);

        /// @brief 今の設定（整えた値）
        const Settings& GetSettings() const { return settings_; }

        /// @brief 今の設定でカスケードごとの初期スペクトルを作り、波高を較正して波の統計を求め直す
        /// @param samples     カスケードごとの書き込み先（nullptr のカスケードは作らない）
        /// @param sampleCount カスケード 1 本分の要素数
        void Build(const std::array<SpectrumSample*, kCascadeCount>& samples, size_t sampleCount);

        /// @brief 今のスペクトルの波の大きさ（高さは振幅倍率を含む。まだ作っていなければすべて 0）
        WaveSummary GetWaveSummary() const;

        /// @brief 波群エンベロープの 3 つの正弦の位相のずれ [rad]（波のエネルギーが群速度で進んだ分）
        /// @param timeSeconds FFT のシミュレーション時刻 [s]
        std::array<float, 3> ComputeWaveGroupPhase(float timeSeconds) const;

        /// @brief 描画される面のカスケードごとの平均二乗傾斜（x・z の傾きの二乗和の平均。振幅倍率を含む）
        float GetCascadeMeanSquareSlope(uint32_t cascadeIndex) const;

        /// @brief カスケードより細かい波（さざ波）の平均二乗傾斜
        /// @details 風速から Cox-Munk の式で求めた海面全体の平均二乗傾斜から、カスケードが持つ分を引いた残り
        float GetFineWaveMeanSquareSlope() const;

        /// @brief カスケードのワールドのパッチ長 [m]
        static float GetCascadePatchLength(uint32_t cascadeIndex);

    private:
        /// @brief 設定を有効範囲へ整える
        static void Sanitize(Settings& settings);

        Settings settings_{};

        /// @brief カスケードごとの平均二乗傾斜（振幅倍率を掛ける前。Build で更新）
        std::array<float, kCascadeCount> cascadeMeanSquareSlope_{};

        /// @brief 波のエネルギーが進む速度（ワールド XZ）[m/s]（Build で更新）
        std::array<float, 2> waveGroupVelocity_{};

        /// @brief 波の大きさ（振幅倍率を掛ける前。Build で更新）
        WaveSummary waveSummary_{};
    };
}
