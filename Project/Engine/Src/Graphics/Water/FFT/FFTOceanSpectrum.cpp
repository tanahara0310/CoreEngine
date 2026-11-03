#include "pch.h"
#include "FFTOceanSpectrum.h"

#include <algorithm>
#include <cmath>
#include <future>

#include "Utility/Logger/Logger.h"

// カスケードの数値はシェーダーと共有する（マクロだけなので HLSL と C++ のどちらからも include できる）
#include "../../../../Assets/Shaders/Water/Common/FFTOceanCascadeValues.hlsli"

namespace CoreEngine
{
    namespace
    {
        static_assert(FFTOceanSpectrum::kCascadeCount == FFT_OCEAN_CASCADE_COUNT,
            "FFTOceanSpectrum::kCascadeCount must match FFT_OCEAN_CASCADE_COUNT in FFTOceanCascadeValues.hlsli");

        // ワールドパッチ長（m）。すべて素数にして互いに素とし、合成波形の繰り返し周期を引き延ばす
        constexpr float kCascadePatchLength[FFTOceanSpectrum::kCascadeCount] = FFT_OCEAN_CASCADE_PATCH_LENGTHS;

        // サンプリング格子の回転（cos/sin）。風向はスペクトル側の逆回転で補正するので、
        // 波の進行方向はワールドで共通のまま
        constexpr float kCascadeRotCos[FFTOceanSpectrum::kCascadeCount] = FFT_OCEAN_CASCADE_ROT_COS;
        constexpr float kCascadeRotSin[FFTOceanSpectrum::kCascadeCount] = FFT_OCEAN_CASCADE_ROT_SIN;

        // 波群エンベロープの正弦の波数ベクトル（ワールド XZ）[rad/m]
        constexpr float kWaveGroupWaveVectorX[3] = FFT_OCEAN_WAVE_GROUP_WAVE_VECTOR_X;
        constexpr float kWaveGroupWaveVectorZ[3] = FFT_OCEAN_WAVE_GROUP_WAVE_VECTOR_Z;

        // カスケードごとの乱数シード（カスケードごとに位相の並びを変える）
        constexpr uint32_t kCascadeRandomSeed[FFTOceanSpectrum::kCascadeCount] = {
            20260626u, 20260626u + 7919u, 20260626u + 2u * 7919u };

        // 帯域境界（波長 m → 波数 rad/m）
        constexpr float kTwoPiValue = 6.28318530718f;
        constexpr float kBandBoundaryLowK = kTwoPiValue / FFT_OCEAN_BAND_LAMBDA_01;
        constexpr float kBandBoundaryHighK = kTwoPiValue / FFT_OCEAN_BAND_LAMBDA_12;
        constexpr float kBandLogHalfWidth = FFT_OCEAN_BAND_LOG_HALFWIDTH;

        // 波高の計算に使う実効風速の範囲 [m/s]（上限で有義波高が飽和する）
        constexpr float kMinHeightWindSpeed = 1.0f;
        constexpr float kMaxHeightWindSpeed = 32.0f;
    }

    FFTOceanSpectrum::FFTOceanSpectrum()
    {
        Sanitize(settings_);
    }

    bool FFTOceanSpectrum::UpdateSettings(const Settings& settings)
    {
        Settings sanitized = settings;
        Sanitize(sanitized);
        sanitized.resolution = settings_.resolution;

        const bool changed =
            sanitized.amplitudeScale != settings_.amplitudeScale ||
            sanitized.windDirection[0] != settings_.windDirection[0] ||
            sanitized.windDirection[1] != settings_.windDirection[1] ||
            sanitized.windSpeed != settings_.windSpeed ||
            sanitized.choppiness != settings_.choppiness ||
            sanitized.activeComponentCount != settings_.activeComponentCount ||
            sanitized.gravity != settings_.gravity ||
            sanitized.fetchMeters != settings_.fetchMeters ||
            sanitized.swellEnabled != settings_.swellEnabled ||
            sanitized.swellHeightMeters != settings_.swellHeightMeters ||
            sanitized.swellPeriodSeconds != settings_.swellPeriodSeconds ||
            sanitized.swellDirection[0] != settings_.swellDirection[0] ||
            sanitized.swellDirection[1] != settings_.swellDirection[1] ||
            sanitized.swellRelativeWidth != settings_.swellRelativeWidth ||
            sanitized.swellSpreadExponent != settings_.swellSpreadExponent;
        if (changed) {
            settings_ = sanitized;
        }
        return changed;
    }

    void FFTOceanSpectrum::Sanitize(Settings& settings)
    {
        FFTOceanSpectrumBuilder::Settings builderSettings{};
        builderSettings.resolution = settings.resolution;
        // パッチ長はカスケードごとの定数なので、ビルダーの値の整えには代表値を渡す
        builderSettings.patchLength = kCascadePatchLength[0];
        builderSettings.amplitudeScale = settings.amplitudeScale;
        builderSettings.windDirection[0] = settings.windDirection[0];
        builderSettings.windDirection[1] = settings.windDirection[1];
        builderSettings.windSpeed = settings.windSpeed;
        builderSettings.fetchMeters = settings.fetchMeters;
        builderSettings.choppiness = settings.choppiness;
        builderSettings.activeComponentCount = settings.activeComponentCount;
        builderSettings.gravity = settings.gravity;

        FFTOceanSpectrumBuilder::SanitizeSettings(builderSettings, kMaxComponents);

        settings.resolution = builderSettings.resolution;
        settings.amplitudeScale = builderSettings.amplitudeScale;
        settings.windDirection[0] = builderSettings.windDirection[0];
        settings.windDirection[1] = builderSettings.windDirection[1];
        settings.windSpeed = builderSettings.windSpeed;
        settings.fetchMeters = builderSettings.fetchMeters;

        // うねり（ビルダーは向きが正規化済みである前提で走る）
        settings.swellHeightMeters = (std::clamp)(settings.swellHeightMeters, 0.0f, 20.0f);
        settings.swellPeriodSeconds = (std::clamp)(settings.swellPeriodSeconds, 2.0f, 30.0f);
        settings.swellRelativeWidth = (std::clamp)(settings.swellRelativeWidth, 0.01f, 0.5f);
        settings.swellSpreadExponent = (std::clamp)(settings.swellSpreadExponent, 1.0f, 60.0f);
        const float swellDirLength = std::sqrt(
            settings.swellDirection[0] * settings.swellDirection[0]
            + settings.swellDirection[1] * settings.swellDirection[1]);
        if (swellDirLength <= 1.0e-4f) {
            settings.swellDirection[0] = 1.0f;
            settings.swellDirection[1] = 0.0f;
        } else {
            settings.swellDirection[0] /= swellDirLength;
            settings.swellDirection[1] /= swellDirLength;
        }
        settings.choppiness = builderSettings.choppiness;
        settings.activeComponentCount = builderSettings.activeComponentCount;
        settings.gravity = builderSettings.gravity;
    }

    void FFTOceanSpectrum::Build(const std::array<SpectrumSample*, kCascadeCount>& samples, size_t sampleCount)
    {
        // カスケードごとに固有のパッチ長で、帯域制限した JONSWAP スペクトルを生成する。
        // 海面は「風波（wind sea）＋ うねり（swell）」の重ね合わせで作る。

        // 風波の波高較正: JONSWAP のフェッチ制限成長則から有義波高 Hs を求め、目標 RMS = Hs/4 とする。
        // カスケード間の配分はスペクトル自身が決める（帯域制限 + Δk² 正規化が前提）。
        const float heightWindSpeed = (std::clamp)(settings_.windSpeed, kMinHeightWindSpeed, kMaxHeightWindSpeed);
        const float gravityValue = (std::max)(settings_.gravity, 0.1f);
        // JONSWAP のフェッチ制限成長則（無次元量）: F* = gF/U², Hs* = 0.0016·√F*。
        // 完全発達（Pierson-Moskowitz）の Hs* = 0.21 を上限にクランプする。
        const float dimensionlessFetch =
            gravityValue * settings_.fetchMeters / (heightWindSpeed * heightWindSpeed);
        const float dimensionlessWaveHeight =
            (std::min)(0.0016f * std::sqrt(dimensionlessFetch), 0.21f);
        const float significantWaveHeight =
            dimensionlessWaveHeight * heightWindSpeed * heightWindSpeed / gravityValue;
        const float windSeaTargetRms = significantWaveHeight * 0.25f;

        // うねりの目標 RMS（Hs/4）とピーク角周波数（周期から）
        const bool swellActive =
            settings_.swellEnabled && settings_.swellHeightMeters > 1.0e-3f
            && settings_.swellPeriodSeconds > 0.1f;
        const float swellTargetRms = swellActive ? settings_.swellHeightMeters * 0.25f : 0.0f;
        const float swellPeakAngularFrequency =
            swellActive ? (kTwoPiValue / settings_.swellPeriodSeconds) : 0.0f;
        const float swellWaveLength = swellActive
            ? kTwoPiValue * gravityValue / (swellPeakAngularFrequency * swellPeakAngularFrequency)
            : 0.0f;

        // カスケード c 用のビルダー設定を組む（成分重みだけを差し替えて 3 回使う）
        const auto makeBuilderSettings =
            [&](uint32_t c, float windWeight, float swellWeight) {
                FFTOceanSpectrumBuilder::Settings s{};
                s.resolution = settings_.resolution;
                s.patchLength = kCascadePatchLength[c];
                s.amplitudeScale = settings_.amplitudeScale;
                // サンプリング格子がカスケードごとに回転しているため、風とうねりの向きは
                // テクスチャ座標系（＝回転後の格子系）へ同じ回転を掛けて渡す
                s.windDirection[0] =
                    kCascadeRotCos[c] * settings_.windDirection[0] - kCascadeRotSin[c] * settings_.windDirection[1];
                s.windDirection[1] =
                    kCascadeRotSin[c] * settings_.windDirection[0] + kCascadeRotCos[c] * settings_.windDirection[1];
                s.swellDirection[0] =
                    kCascadeRotCos[c] * settings_.swellDirection[0] - kCascadeRotSin[c] * settings_.swellDirection[1];
                s.swellDirection[1] =
                    kCascadeRotSin[c] * settings_.swellDirection[0] + kCascadeRotCos[c] * settings_.swellDirection[1];
                s.windSpeed = settings_.windSpeed;
                s.fetchMeters = settings_.fetchMeters;
                s.choppiness = settings_.choppiness;
                s.activeComponentCount = settings_.activeComponentCount;
                s.gravity = settings_.gravity;
                s.randomSeed = kCascadeRandomSeed[c];
                // 帯域制限（相補窓）。境界は全カスケード共通で、担当帯は index で決まる
                s.cascadeIndex = c;
                s.bandBoundaryLowK = kBandBoundaryLowK;
                s.bandBoundaryHighK = kBandBoundaryHighK;
                s.bandLogHalfWidth = kBandLogHalfWidth;
                s.swellPeakAngularFrequency = swellPeakAngularFrequency;
                s.swellRelativeWidth = settings_.swellRelativeWidth;
                s.swellSpreadExponent = settings_.swellSpreadExponent;
                s.windSeaVarianceWeight = windWeight;
                s.swellVarianceWeight = swellWeight;
                // 較正はここで行うのでビルダー内の正規化は使わない
                s.targetRmsHeight = 0.0f;
                return s;
            };

        // 全カスケードを一度生成して総 RMS（二乗和平方根）を返す。
        // カスケードは互いに素な波数帯 × 独立位相なので分散が加算される。
        // outGroupVelocity には、全カスケードのエネルギーで重み付けした群速度（ワールド XZ）を書く。
        const auto buildAndMeasure =
            [&](float windWeight, float swellWeight, float* outPerCascadeRms, float* outPerCascadeMss,
                float* outGroupVelocity) {
                // カスケードは書き込み先が別なので、別のスレッドで並べて作る
                std::array<std::future<FFTOceanSpectrumBuilder::BuildStats>, kCascadeCount> builds;
                for (uint32_t c = 0; c < kCascadeCount; ++c) {
                    if (!samples[c]) {
                        continue;
                    }
                    builds[c] = std::async(std::launch::async,
                        [s = makeBuilderSettings(c, windWeight, swellWeight),
                         cascadeSamples = samples[c], sampleCount]() {
                            return FFTOceanSpectrumBuilder::BuildSpectrum(s, cascadeSamples, sampleCount);
                        });
                }

                double sumOfSquares = 0.0;
                double energySum = 0.0;
                double groupVelocitySum[2] = { 0.0, 0.0 };
                for (uint32_t c = 0; c < kCascadeCount; ++c) {
                    if (!builds[c].valid()) {
                        continue;
                    }
                    const FFTOceanSpectrumBuilder::BuildStats stats = builds[c].get();
                    if (outPerCascadeRms) {
                        outPerCascadeRms[c] = stats.measuredRmsHeight;
                    }
                    if (outPerCascadeMss) {
                        outPerCascadeMss[c] = stats.meanSquareSlope;
                    }
                    sumOfSquares +=
                        static_cast<double>(stats.measuredRmsHeight) * stats.measuredRmsHeight;

                    // 回転格子系の群速度をワールドへ逆回転して足す
                    const double gx = stats.energyWeightedGroupVelocity[0];
                    const double gz = stats.energyWeightedGroupVelocity[1];
                    groupVelocitySum[0] += kCascadeRotCos[c] * gx + kCascadeRotSin[c] * gz;
                    groupVelocitySum[1] += -kCascadeRotSin[c] * gx + kCascadeRotCos[c] * gz;
                    energySum += stats.energySum;
                }
                if (outGroupVelocity) {
                    outGroupVelocity[0] = energySum > 0.0 ? static_cast<float>(groupVelocitySum[0] / energySum) : 0.0f;
                    outGroupVelocity[1] = energySum > 0.0 ? static_cast<float>(groupVelocitySum[1] / energySum) : 0.0f;
                }
                return static_cast<float>(std::sqrt(sumOfSquares));
            };

        // ---- パス1: 風波のみの生RMS ----
        const float rawWindSeaRms = buildAndMeasure(1.0f, 0.0f, nullptr, nullptr, nullptr);
        // ---- パス2: うねりのみの生RMS ----
        const float rawSwellRms = swellActive ? buildAndMeasure(0.0f, 1.0f, nullptr, nullptr, nullptr) : 0.0f;

        // 成分ごとの分散重み。密度に掛かるので (目標RMS / 生RMS)² になる。
        // これで合成後の総RMSが sqrt(windTarget² + swellTarget²) に一致する。
        const float windSeaWeight = (rawWindSeaRms > 1.0e-9f)
            ? (windSeaTargetRms / rawWindSeaRms) * (windSeaTargetRms / rawWindSeaRms) : 0.0f;
        const float swellWeight = (rawSwellRms > 1.0e-9f)
            ? (swellTargetRms / rawSwellRms) * (swellTargetRms / rawSwellRms) : 0.0f;

        // ---- パス3: 較正済みの重みで本生成 ----
        float finalRmsHeight[kCascadeCount] = {};
        const float finalTotalRms = buildAndMeasure(
            windSeaWeight, swellWeight, finalRmsHeight, cascadeMeanSquareSlope_.data(), waveGroupVelocity_.data());

        // ピーク波長 λp = 2πg/ωp²。ビルダーと同じ式でピーク角周波数を求める
        const float peakAngularFrequency = (std::max)(
            22.0f * std::pow(
                gravityValue * gravityValue / (heightWindSpeed * settings_.fetchMeters), 1.0f / 3.0f),
            0.877f * gravityValue / heightWindSpeed);
        const float peakWaveLength =
            kTwoPiValue * gravityValue / (peakAngularFrequency * peakAngularFrequency);

        waveSummary_.windSeaWavelength = peakWaveLength;
        waveSummary_.windSeaHeight = significantWaveHeight;
        waveSummary_.swellWavelength = swellWaveLength;
        waveSummary_.swellHeight = swellActive ? settings_.swellHeightMeters : 0.0f;

        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::Pipeline,
            "FFTOceanSpectrum: spectrum calibrated (JONSWAP). windSpeed={:.2f} fetch={:.0f}km windSeaLambda={:.1f}m windSeaHs={:.2f} | "
            "swell={} swellHs={:.2f} swellPeriod={:.1f}s swellLambda={:.1f}m | totalRms={:.3f} (target {:.3f}) "
            "cascadeRms=[{:.3f}, {:.3f}, {:.3f}] share=[{:.1f}%, {:.1f}%, {:.1f}%] "
            "meanSquareSlope=[{:.5f}, {:.5f}, {:.5f}] groupVelocity=({:.2f}, {:.2f})m/s",
            heightWindSpeed,
            settings_.fetchMeters / 1000.0f,
            peakWaveLength,
            significantWaveHeight,
            swellActive ? "on" : "off",
            settings_.swellHeightMeters,
            settings_.swellPeriodSeconds,
            swellWaveLength,
            finalTotalRms,
            std::sqrt(windSeaTargetRms * windSeaTargetRms + swellTargetRms * swellTargetRms),
            finalRmsHeight[0],
            finalRmsHeight[1],
            finalRmsHeight[2],
            100.0f * finalRmsHeight[0] / (std::max)(finalTotalRms, 1.0e-6f),
            100.0f * finalRmsHeight[1] / (std::max)(finalTotalRms, 1.0e-6f),
            100.0f * finalRmsHeight[2] / (std::max)(finalTotalRms, 1.0e-6f),
            GetCascadeMeanSquareSlope(0),
            GetCascadeMeanSquareSlope(1),
            GetCascadeMeanSquareSlope(2),
            waveGroupVelocity_[0],
            waveGroupVelocity_[1]);
    }

    FFTOceanSpectrum::WaveSummary FFTOceanSpectrum::GetWaveSummary() const
    {
        WaveSummary summary = waveSummary_;
        summary.windSeaHeight *= settings_.amplitudeScale;
        summary.swellHeight *= settings_.amplitudeScale;
        return summary;
    }

    std::array<float, 3> FFTOceanSpectrum::ComputeWaveGroupPhase(float timeSeconds) const
    {
        constexpr double kTwoPi = 6.283185307179586;
        std::array<float, 3> phase{};
        for (size_t i = 0; i < phase.size(); ++i) {
            const double angularSpeed =
                static_cast<double>(waveGroupVelocity_[0]) * kWaveGroupWaveVectorX[i]
                + static_cast<double>(waveGroupVelocity_[1]) * kWaveGroupWaveVectorZ[i];
            phase[i] = static_cast<float>(std::fmod(angularSpeed * static_cast<double>(timeSeconds), kTwoPi));
        }
        return phase;
    }

    float FFTOceanSpectrum::GetCascadeMeanSquareSlope(uint32_t cascadeIndex) const
    {
        if (cascadeIndex >= kCascadeCount) {
            return 0.0f;
        }
        return cascadeMeanSquareSlope_[cascadeIndex] * settings_.amplitudeScale * settings_.amplitudeScale;
    }

    float FFTOceanSpectrum::GetFineWaveMeanSquareSlope() const
    {
        // Cox-Munk（油膜の無い海面）: 平均二乗傾斜 = 0.003 + 5.12e-3 × 風速 [m/s]
        const float totalMeanSquareSlope = 0.003f + 5.12e-3f * (std::max)(settings_.windSpeed, 0.0f);
        float resolvedMeanSquareSlope = 0.0f;
        for (uint32_t c = 0; c < kCascadeCount; ++c) {
            resolvedMeanSquareSlope += GetCascadeMeanSquareSlope(c);
        }
        return (std::max)(totalMeanSquareSlope - resolvedMeanSquareSlope, 0.0f);
    }

    float FFTOceanSpectrum::GetCascadePatchLength(uint32_t cascadeIndex)
    {
        if (cascadeIndex >= kCascadeCount) {
            return 0.0f;
        }
        return kCascadePatchLength[cascadeIndex];
    }
}
