#include "pch.h"
#include "FFTOceanSpectrumBuilder.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#include "Math/MathCore.h"

namespace CoreEngine
{
    namespace
    {
        constexpr float kPi = MathCore::Constants::kPi;
        constexpr float kTwoPi = MathCore::Constants::kTwoPi;

        uint32_t MirrorCoord(uint32_t value, uint32_t resolution)
        {
            return (resolution - value) & (resolution - 1);
        }

        /// @brief ln k 上の smoothstep（境界 kBoundary の下で 0、上で 1）
        /// @note 遷移域を対数波数で取るのがここ固有の事情。
        ///       補間そのものは MathCore::SmoothStep と同じエルミート。
        float LogSmoothStep(float waveNumber, float boundaryWaveNumber, float logHalfWidth)
        {
            const float halfWidth = (std::max)(logHalfWidth, 1.0e-3f);
            const float t = 0.5f + (std::log(waveNumber) - std::log(boundaryWaveNumber)) / (2.0f * halfWidth);
            return MathCore::SmoothStep(t);
        }

        /// @brief このカスケードが担当するパワー比 [0,1]
        /// @details 3 カスケードぶんを計算して和で割るため、分割は厳密に 1 になる
        ///          （境界の遷移域が重なっても取りこぼし・二重計上が出ない）。
        ///          振幅へ掛けるときは sqrt を取ること（分散が分割対象なので）。
        float ComputeBandPowerWeight(
            float waveNumber, uint32_t cascadeIndex,
            float boundaryLowK, float boundaryHighK, float logHalfWidth)
        {
            if (boundaryLowK <= 0.0f || boundaryHighK <= 0.0f) {
                return 1.0f; // 帯域制限なし（従来動作）
            }

            const float sLow = LogSmoothStep(waveNumber, boundaryLowK, logHalfWidth);
            const float sHigh = LogSmoothStep(waveNumber, boundaryHighK, logHalfWidth);

            const float weights[3] = {
                1.0f - sLow,            // 長波側（境界1より下）
                sLow * (1.0f - sHigh),  // 中間帯
                sHigh,                  // 短波側（境界2より上）
            };
            const float sum = weights[0] + weights[1] + weights[2];
            if (sum <= 1.0e-6f) {
                return 0.0f;
            }
            const uint32_t index = (std::min)(cascadeIndex, static_cast<uint32_t>(2));
            return weights[index] / sum;
        }

        // ===== JONSWAP スペクトル =====
        // ピークが風速とフェッチ（吹送距離）の両方で決まり、γ で峰が尖る（＝うねりが波群として揃う）。
        // 高波数側の裾は ω^-5 ＝ 波数空間で k^-3 の平衡則。
        // 振幅の絶対値は呼び出し側の RMS 較正が吸収するので、ここでは「形」だけを作る。

        constexpr float kJonswapGamma = 3.3f;

        /// @brief フェッチ制限された JONSWAP のピーク角周波数 [rad/s]
        /// @details 完全発達（Pierson-Moskowitz, ωp = 0.877 g/U）を下限にクランプする。
        ///          フェッチをいくら伸ばしても海は完全発達より育たない。
        float ComputePeakAngularFrequency(float windSpeed, float fetchMeters, float gravity)
        {
            const float speed = (std::max)(windSpeed, 0.1f);
            const float fetch = (std::max)(fetchMeters, 1.0f);
            const float fetchLimited =
                22.0f * std::pow(gravity * gravity / (speed * fetch), 1.0f / 3.0f);
            const float fullyDeveloped = 0.877f * gravity / speed;
            return (std::max)(fetchLimited, fullyDeveloped);
        }

        /// @brief JONSWAP の周波数スペクトル形状（定数倍は省略）
        float ComputeJonswapShape(float angularFrequency, float peakAngularFrequency)
        {
            if (angularFrequency <= 1.0e-4f) {
                return 0.0f;
            }
            const float peakRatio = peakAngularFrequency / angularFrequency;
            const float peakRatio4 = peakRatio * peakRatio * peakRatio * peakRatio;
            const float baseShape =
                std::exp(-1.25f * peakRatio4) / std::pow(angularFrequency, 5.0f);

            // 峰の増幅（γ^r）。σ はピークの前後で切り替わる標準値。
            const float sigma = (angularFrequency <= peakAngularFrequency) ? 0.07f : 0.09f;
            const float delta = angularFrequency - peakAngularFrequency;
            const float denominator =
                2.0f * sigma * sigma * peakAngularFrequency * peakAngularFrequency;
            const float peakEnhancementExponent =
                std::exp(-(delta * delta) / (std::max)(denominator, 1.0e-8f));

            return baseShape * std::pow(kJonswapGamma, peakEnhancementExponent);
        }

        /// @brief 指向拡がりの鋭さ s（Mitsuyasu / Hasselmann）
        /// @details 実海ではピーク付近の波が風向に鋭く揃い、離れた波ほど横に広がる。
        ///          cos²θ 固定だと短波まで風向に張り付いて筋状に見えてしまう。
        float ComputeSpreadExponent(float angularFrequency, float peakAngularFrequency)
        {
            const float ratio = angularFrequency / (std::max)(peakAngularFrequency, 1.0e-4f);
            const float exponent = (ratio < 1.0f) ? 4.06f : -2.34f;
            return (std::clamp)(9.77f * std::pow(ratio, exponent), 0.5f, 40.0f);
        }

        /// @brief 正規化済み指向分布 D(θ) = Q(s)·cos^{2s}(θ/2)（∫D dθ = 1）
        /// @details Q(s) を掛けないと、s が k ごとに変わることでスペクトルの
        ///          波数分布そのものが歪む（指向を鋭くした帯だけエネルギーが増える）。
        ///          Γ の直接評価は s が大きいと溢れるので lgamma で対数計算する。
        float ComputeDirectionalSpread(float angleFromWind, float spreadExponent)
        {
            const float halfAngleCos = std::cos(0.5f * angleFromWind);
            if (halfAngleCos <= 0.0f) {
                return 0.0f; // 風の真後ろ（θ=π）は 0。旧実装の逆風 0.25 倍ハックを置き換える
            }
            const float s = spreadExponent;
            const float logNormalizer =
                (2.0f * s - 1.0f) * 0.69314718f            // (2s-1)·ln2
                - 1.14472989f                              // -ln(π)
                + 2.0f * std::lgamma(s + 1.0f)
                - std::lgamma(2.0f * s + 1.0f);
            return std::exp(logNormalizer) * std::pow(halfAngleCos, 2.0f * s);
        }

        /// @brief うねり成分の周波数スペクトル形状（ωs 周りの狭いガウス）
        /// @details うねりは発生源から遠く離れて分散した結果なので、周期がほぼ揃った狭帯域になる。
        ///          ガウスで書くと「周期と鋭さ」を直接操作できて調整しやすい。
        float ComputeSwellShape(
            float angularFrequency, float peakAngularFrequency, float relativeWidth)
        {
            if (peakAngularFrequency <= 1.0e-4f) {
                return 0.0f;
            }
            const float width = (std::max)(relativeWidth, 1.0e-3f) * peakAngularFrequency;
            const float delta = angularFrequency - peakAngularFrequency;
            return std::exp(-(delta * delta) / (2.0f * width * width));
        }
    }

    void FFTOceanSpectrumBuilder::ScaleSpectrum(
        SpectrumSample* spectrumSamples, size_t sampleCount, float scale)
    {
        if (!spectrumSamples) {
            return;
        }
        for (size_t i = 0; i < sampleCount; ++i) {
            spectrumSamples[i].h0[0] *= scale;
            spectrumSamples[i].h0[1] *= scale;
            spectrumSamples[i].h0Minus[0] *= scale;
            spectrumSamples[i].h0Minus[1] *= scale;
        }
    }

    void FFTOceanSpectrumBuilder::SanitizeSettings(Settings& settings, uint32_t maxSpectrumComponents)
    {
        settings.resolution = (std::clamp)(settings.resolution, static_cast<uint32_t>(8), static_cast<uint32_t>(512));
        uint32_t powerOfTwo = 8;
        while (powerOfTwo < settings.resolution && powerOfTwo < static_cast<uint32_t>(512)) {
            powerOfTwo <<= 1;
        }

        settings.resolution = powerOfTwo;
        settings.patchLength = (std::max)(settings.patchLength, 1.0f);
        settings.amplitudeScale = (std::max)(settings.amplitudeScale, 0.0f);
        settings.windSpeed = (std::max)(settings.windSpeed, 0.0f);
        settings.fetchMeters = (std::clamp)(settings.fetchMeters, 1000.0f, 2.0e6f);
        settings.choppiness = (std::max)(settings.choppiness, 0.0f);
        settings.activeComponentCount = (std::clamp)(settings.activeComponentCount, static_cast<uint32_t>(1), maxSpectrumComponents);
        settings.gravity = (std::max)(settings.gravity, 0.1f);

        const float windDirX = settings.windDirection[0];
        const float windDirY = settings.windDirection[1];
        const float windLength = std::sqrt(windDirX * windDirX + windDirY * windDirY);
        if (windLength <= 1.0e-4f) {
            settings.windDirection[0] = 1.0f;
            settings.windDirection[1] = 0.0f;
        } else {
            settings.windDirection[0] = windDirX / windLength;
            settings.windDirection[1] = windDirY / windLength;
        }
    }

    FFTOceanSpectrumBuilder::BuildStats FFTOceanSpectrumBuilder::BuildSpectrum(
        const Settings& settings,
        SpectrumSample* outSpectrumSamples,
        size_t outSampleCount)
    {
        BuildStats stats{};
        if (!outSpectrumSamples) {
            return stats;
        }

        const uint32_t resolution = settings.resolution;
        const uint32_t sampleCount = resolution * resolution;
        if (outSampleCount < static_cast<size_t>(sampleCount)) {
            return stats;
        }

        const float patchLength = settings.patchLength;
        const float gravity = settings.gravity;
        const float windSpeed = (std::max)(settings.windSpeed, 0.1f);
        const float windX = settings.windDirection[0];
        const float windY = settings.windDirection[1];
        const float maxWaveNumber = kPi * static_cast<float>(resolution) / patchLength;

        // JONSWAP のピーク角周波数（風速とフェッチで決まる。完全発達でクランプ）
        const float peakAngularFrequency =
            ComputePeakAngularFrequency(windSpeed, settings.fetchMeters, gravity);

        // ★カスケード間の相対エネルギーを正しくするための離散化正規化★
        // スペクトル密度 Ψ(k) は単位波数面積あたりの分散なので、離散和にはセル面積 Δk² が要る。
        // Δk = 2π/patchLength はカスケードごとに違うため、掛けないと大パッチだけ (L/2π)² 倍に膨れる。
        const float deltaWaveNumber = kTwoPi / patchLength;
        const float cellArea = deltaWaveNumber * deltaWaveNumber;

        struct TempSpectrumSample {
            float real = 0.0f;
            float imag = 0.0f;
            float waveVectorX = 0.0f;
            float waveVectorY = 0.0f;
            float angularFrequency = 0.0f;
            float normalizedBand = 0.0f;
        };

        std::vector<TempSpectrumSample> tempSpectrum(sampleCount);
        std::mt19937 rng(settings.randomSeed);
        std::normal_distribution<float> gaussianDistribution(0.0f, 1.0f);
        float accumulatedSpectralAmplitude = 0.0f;
        double accumulatedAmplitudeSquared = 0.0;

        for (uint32_t y = 0; y < resolution; ++y) {
            for (uint32_t x = 0; x < resolution; ++x) {
                const uint32_t index = y * resolution + x;
                const int32_t centeredX = static_cast<int32_t>(x) - static_cast<int32_t>(resolution / 2);
                const int32_t centeredY = static_cast<int32_t>(y) - static_cast<int32_t>(resolution / 2);
                const float waveVectorX = static_cast<float>(centeredX) * (kTwoPi / patchLength);
                const float waveVectorY = static_cast<float>(centeredY) * (kTwoPi / patchLength);
                const float waveNumberSquared = waveVectorX * waveVectorX + waveVectorY * waveVectorY;

                TempSpectrumSample& sample = tempSpectrum[index];
                sample.waveVectorX = waveVectorX;
                sample.waveVectorY = waveVectorY;

                if (waveNumberSquared <= 1.0e-8f) {
                    continue;
                }

                const float waveNumber = std::sqrt(waveNumberSquared);
                const float directionX = waveVectorX / waveNumber;
                const float directionY = waveVectorY / waveNumber;

                // 深水波の分散関係 ω = √(gk)
                const float angularFrequency = std::sqrt(gravity * waveNumber);

                // ---- 全方位の波数スペクトル S(k) ----
                // JONSWAP は周波数スペクトル S(ω) なので、ヤコビアン dω/dk で
                // 波数空間へ移す（ω=√(gk) → dω/dk = ½√(g/k)）。
                const float omegaOverK = 0.5f * std::sqrt(gravity / waveNumber);
                const float omniDirectionalSpectrum =
                    ComputeJonswapShape(angularFrequency, peakAngularFrequency) * omegaOverK;

                // ---- 指向分布 D(θ)（周波数依存の拡がり）----
                const float waveAlignment =
                    (std::clamp)(directionX * windX + directionY * windY, -1.0f, 1.0f);
                const float angleFromWind = std::acos(waveAlignment);
                const float spreadExponent =
                    ComputeSpreadExponent(angularFrequency, peakAngularFrequency);
                const float directionalSpread =
                    ComputeDirectionalSpread(angleFromWind, spreadExponent);

                // ---- 2D スペクトル密度 Ψ(k,θ) = S(k)·D(θ)/k ----
                // （極座標の面積要素 k·dk·dθ で積分すると分散になるよう 1/k を掛ける）
                float spectralDensity =
                    settings.windSeaVarianceWeight
                    * omniDirectionalSpectrum * directionalSpread / waveNumber;

                // ---- うねり成分を重ね合わせる ----
                // 風波とは別の向き・別の周期を持つ独立な確率場として密度を加算する
                // （位相は同じガウス乱数から引くが、密度が加算されることで
                //   分散が加算され、2 つの波列が重なった海面になる）。
                if (settings.swellVarianceWeight > 0.0f
                    && settings.swellPeakAngularFrequency > 1.0e-4f) {
                    const float swellAlignment = (std::clamp)(
                        directionX * settings.swellDirection[0]
                        + directionY * settings.swellDirection[1], -1.0f, 1.0f);
                    const float swellSpread = ComputeDirectionalSpread(
                        std::acos(swellAlignment), settings.swellSpreadExponent);
                    const float swellOmni = ComputeSwellShape(
                        angularFrequency,
                        settings.swellPeakAngularFrequency,
                        settings.swellRelativeWidth) * omegaOverK;
                    spectralDensity +=
                        settings.swellVarianceWeight * swellOmni * swellSpread / waveNumber;
                }

                // 帯域制限: このカスケードが担当するパワー比だけを取り出す。
                // 分散（パワー）の分割なので、振幅には sqrt を掛ける。
                // これで「3 カスケードが同じ波長を重複して持つ」二重・三重計上と、
                // 「小パッチのエネルギーが自分のタイル基本波に積み上がる」現象が同時に消える。
                const float bandPowerWeight = ComputeBandPowerWeight(
                    waveNumber,
                    settings.cascadeIndex,
                    settings.bandBoundaryLowK,
                    settings.bandBoundaryHighK,
                    settings.bandLogHalfWidth);
                spectralDensity *= bandPowerWeight;

                // 離散セル面積を掛けて「このセルが持つ分散」にする（上のコメント参照）
                const float cellVariance = spectralDensity * cellArea;

                const float spectralAmplitude = std::sqrt((std::max)(cellVariance, 0.0f)) * 0.70710678f;
                sample.real = gaussianDistribution(rng) * spectralAmplitude;
                sample.imag = gaussianDistribution(rng) * spectralAmplitude;
                sample.angularFrequency = angularFrequency;
                sample.normalizedBand = (std::min)(waveNumber / (std::max)(maxWaveNumber, 1.0e-4f), 1.0f);

                if (spectralAmplitude > 0.0f) {
                    ++stats.activeSpectrumSampleCount;
                    accumulatedSpectralAmplitude += spectralAmplitude;
                    accumulatedAmplitudeSquared += static_cast<double>(spectralAmplitude) * spectralAmplitude;
                    stats.maxSpectralAmplitude = (std::max)(stats.maxSpectralAmplitude, spectralAmplitude);
                    stats.maxAngularFrequency = (std::max)(stats.maxAngularFrequency, sample.angularFrequency);
                }
            }
        }

        // ---- 波高の物理較正 ----
        // h(x) = (1/N)Σ h̃(k)e^{ikx} で各 h0 の実部/虚部が N(0, sa²) なので、
        // RMS = 2·sqrt(Σ sa²)/N が生成スペクトルから決定論的に見積もれる。
        // targetRmsHeight が指定されたら全モードを一様スケールして目標 RMS へ合わせる。
        stats.measuredRmsHeight = static_cast<float>(
            2.0 * std::sqrt(accumulatedAmplitudeSquared) / static_cast<double>(resolution));
        if (settings.targetRmsHeight > 0.0f && stats.measuredRmsHeight > 1.0e-6f) {
            const float heightScale = settings.targetRmsHeight / stats.measuredRmsHeight;
            stats.appliedHeightScale = heightScale;
            for (TempSpectrumSample& sample : tempSpectrum) {
                sample.real *= heightScale;
                sample.imag *= heightScale;
            }
            stats.maxSpectralAmplitude *= heightScale;
            accumulatedSpectralAmplitude *= heightScale;
        }

        for (uint32_t y = 0; y < resolution; ++y) {
            for (uint32_t x = 0; x < resolution; ++x) {
                const uint32_t index = y * resolution + x;
                const uint32_t mirroredX = MirrorCoord(x, resolution);
                const uint32_t mirroredY = MirrorCoord(y, resolution);
                const uint32_t mirroredIndex = mirroredY * resolution + mirroredX;

                outSpectrumSamples[index].h0[0] = tempSpectrum[index].real;
                outSpectrumSamples[index].h0[1] = tempSpectrum[index].imag;
                outSpectrumSamples[index].h0Minus[0] = tempSpectrum[mirroredIndex].real;
                outSpectrumSamples[index].h0Minus[1] = -tempSpectrum[mirroredIndex].imag;
                outSpectrumSamples[index].waveVector[0] = tempSpectrum[index].waveVectorX;
                outSpectrumSamples[index].waveVector[1] = tempSpectrum[index].waveVectorY;
                outSpectrumSamples[index].angularFrequency = tempSpectrum[index].angularFrequency;
                outSpectrumSamples[index].directionalWeight = tempSpectrum[index].normalizedBand;
            }
        }

        const uint32_t probeX = resolution / 2 + 1;
        const uint32_t probeY = resolution / 2;
        stats.probeIndex = probeY * resolution + probeX;
        stats.probeSample = outSpectrumSamples[stats.probeIndex];
        stats.averageSpectralAmplitude = stats.activeSpectrumSampleCount > 0
            ? accumulatedSpectralAmplitude / static_cast<float>(stats.activeSpectrumSampleCount)
            : 0.0f;

        return stats;
    }
}
