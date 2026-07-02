#include "pch.h"
#include "FFTOceanSpectrumBuilder.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace CoreEngine
{
    namespace
    {
        constexpr float kPi = 3.14159265359f;
        constexpr float kTwoPi = 6.28318530718f;

        uint32_t MirrorCoord(uint32_t value, uint32_t resolution)
        {
            return (resolution - value) & (resolution - 1);
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
        const float largestWave = (windSpeed * windSpeed) / gravity;
        const float dampingLength = largestWave * 0.001f;
        const float maxWaveNumber = kPi * static_cast<float>(resolution) / patchLength;

        struct TempSpectrumSample {
            float real = 0.0f;
            float imag = 0.0f;
            float waveVectorX = 0.0f;
            float waveVectorY = 0.0f;
            float angularFrequency = 0.0f;
            float normalizedBand = 0.0f;
        };

        std::vector<TempSpectrumSample> tempSpectrum(sampleCount);
        std::mt19937 rng(20260626);
        std::normal_distribution<float> gaussianDistribution(0.0f, 1.0f);
        float accumulatedSpectralAmplitude = 0.0f;

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
                const float waveAlignment = directionX * windX + directionY * windY;
                const float largestWaveSquared = largestWave * largestWave;
                const float dampingSquared = dampingLength * dampingLength;
                float phillipsSpectrum = std::exp(-1.0f / (waveNumberSquared * largestWaveSquared));
                phillipsSpectrum /= waveNumberSquared * waveNumberSquared;
                phillipsSpectrum *= waveAlignment * waveAlignment;
                phillipsSpectrum *= std::exp(-waveNumberSquared * dampingSquared);
                if (waveAlignment < 0.0f) {
                    phillipsSpectrum *= 0.25f;
                }

                const float spectralAmplitude = std::sqrt((std::max)(phillipsSpectrum, 0.0f)) * 0.70710678f;
                sample.real = gaussianDistribution(rng) * spectralAmplitude;
                sample.imag = gaussianDistribution(rng) * spectralAmplitude;
                sample.angularFrequency = std::sqrt(gravity * waveNumber);
                sample.normalizedBand = (std::min)(waveNumber / (std::max)(maxWaveNumber, 1.0e-4f), 1.0f);

                if (spectralAmplitude > 0.0f) {
                    ++stats.activeSpectrumSampleCount;
                    accumulatedSpectralAmplitude += spectralAmplitude;
                    stats.maxSpectralAmplitude = (std::max)(stats.maxSpectralAmplitude, spectralAmplitude);
                    stats.maxAngularFrequency = (std::max)(stats.maxAngularFrequency, sample.angularFrequency);
                }
            }
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
