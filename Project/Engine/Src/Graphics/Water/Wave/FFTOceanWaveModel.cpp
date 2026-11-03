#include "pch.h"
#include "FFTOceanWaveModel.h"

#include <cmath>

#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/Water/FFT/FFTOceanManager.h"
#include "Graphics/Water/Surface/WaterRenderResources.h"
#include "Graphics/Water/Surface/WaterSurfaceComponent.h"
#include "Graphics/Water/WaterSurfaceData.h"

namespace CoreEngine
{
    namespace {
        /// @brief FFT のスペクトルを決める値が同じか（波のモデルの選択は見ない）
        bool SameSpectrum(const WaterWaveSettings& a, const WaterWaveSettings& b)
        {
            return a.amplitudeScale == b.amplitudeScale
                && a.windDirection.x == b.windDirection.x
                && a.windDirection.y == b.windDirection.y
                && a.windSpeed == b.windSpeed
                && a.fetchMeters == b.fetchMeters
                && a.choppiness == b.choppiness
                && a.activeComponentCount == b.activeComponentCount
                && a.swellEnabled == b.swellEnabled
                && a.swellHeightMeters == b.swellHeightMeters
                && a.swellPeriodSeconds == b.swellPeriodSeconds
                && a.swellDirection.x == b.swellDirection.x
                && a.swellDirection.y == b.swellDirection.y
                && a.swellRelativeWidth == b.swellRelativeWidth
                && a.swellSpreadExponent == b.swellSpreadExponent;
        }
    }

    void FFTOceanWaveModel::Advance(float deltaSeconds, [[maybe_unused]] bool drawn)
    {
        time_ += deltaSeconds;
    }

    void FFTOceanWaveModel::ApplySettings(RenderDomainContext& domain, const WaterWaveSettings& settings)
    {
        FFTOceanManager* const fftOcean = domain.GetFFTOceanManager();
        if (!fftOcean) {
            return;
        }
        if (appliedSettings_ && SameSpectrum(*appliedSettings_, settings)) {
            return;
        }
        appliedSettings_ = settings;

        // 変更の検知・値の整え・GPU の待ち合わせ・スペクトルの作り直しは SetSettings が受け持つ
        FFTOceanManager::Settings managerSettings = fftOcean->GetSettings();
        managerSettings.amplitudeScale = settings.amplitudeScale;
        managerSettings.windDirection[0] = settings.windDirection.x;
        managerSettings.windDirection[1] = settings.windDirection.y;
        managerSettings.windSpeed = settings.windSpeed;
        managerSettings.fetchMeters = settings.fetchMeters;
        managerSettings.choppiness = settings.choppiness;
        managerSettings.activeComponentCount = settings.activeComponentCount;
        managerSettings.swellEnabled = settings.swellEnabled;
        managerSettings.swellHeightMeters = settings.swellHeightMeters;
        managerSettings.swellPeriodSeconds = settings.swellPeriodSeconds;
        managerSettings.swellDirection[0] = settings.swellDirection.x;
        managerSettings.swellDirection[1] = settings.swellDirection.y;
        managerSettings.swellRelativeWidth = settings.swellRelativeWidth;
        managerSettings.swellSpreadExponent = settings.swellSpreadExponent;
        fftOcean->SetSettings(managerSettings);
    }

    void FFTOceanWaveModel::ApplyToSurface(RenderDomainContext& domain, WaterSurfaceComponent& waterPlane) const
    {
        // 波群エンベロープの位相（今の時刻までに波のエネルギーが群速度で進んだ分）
        if (const FFTOceanManager* fftOcean = domain.GetFFTOceanManager()) {
            waterPlane.SetWaveGroupPhase(fftOcean->GetSpectrum().ComputeWaveGroupPhase(time_));
        }
    }

    void FFTOceanWaveModel::Capture(
        RenderDomainContext& domain,
        const WaterSurfaceComponent& waterPlane,
        WaterSurfaceData& snapshot) const
    {
        snapshot = {};
        snapshot.waterHeight = waterPlane.GetWaterLevel();
        snapshot.activeWaveCount = 0;
        snapshot.time = time_;
        snapshot.simulationType = static_cast<uint32_t>(GetType());
        snapshot.fftOcean = BuildInput(domain);
    }

    float FFTOceanWaveModel::ComputeSignificantWaveHeight(
        RenderDomainContext& domain, [[maybe_unused]] const WaterSurfaceData& snapshot) const
    {
        const FFTOceanManager* const fftOcean = domain.GetFFTOceanManager();
        if (!fftOcean) {
            return 0.0f;
        }
        const FFTOceanSpectrum::WaveSummary summary = fftOcean->GetSpectrum().GetWaveSummary();
        return std::sqrt(summary.windSeaHeight * summary.windSeaHeight + summary.swellHeight * summary.swellHeight);
    }

    void FFTOceanWaveModel::Bind(RenderDomainContext& domain, WaterFrameBinding& binding) const
    {
        const FFTOceanManager* const fftOcean = domain.GetFFTOceanManager();
        if (!fftOcean) {
            return;
        }
        binding.resources.fftDisplacementSRV = fftOcean->GetDisplacementSRVHandle();
        binding.resources.fftNormalSRV = fftOcean->GetNormalSRVHandle();
        binding.resources.fftJacobianSRV = fftOcean->GetJacobianSRVHandle();
        const FFTOceanSpectrum& spectrum = fftOcean->GetSpectrum();
        static_assert(FFTOceanSpectrum::kCascadeCount == 3, "fftCascadeMeanSquareSlope のカスケード数と一致させる");
        for (uint32_t c = 0; c < FFTOceanSpectrum::kCascadeCount; ++c) {
            binding.fftCascadeMeanSquareSlope[c] = spectrum.GetCascadeMeanSquareSlope(c);
        }
        binding.fineWaveMeanSquareSlope = spectrum.GetFineWaveMeanSquareSlope();
    }

    WaterFFTOceanInput FFTOceanWaveModel::BuildInput(RenderDomainContext& domain) const
    {
        WaterFFTOceanInput input{};
        const FFTOceanManager* const fftOcean = domain.GetFFTOceanManager();
        if (!fftOcean || !fftOcean->IsInitialized()) {
            return input;
        }
        input.displacementSRV = fftOcean->GetDisplacementSRVHandle();
        input.normalSRV = fftOcean->GetNormalSRVHandle();
        input.resolution = fftOcean->GetSettings().resolution;
        input.enabled = 1;
        const FFTOceanSpectrum& spectrum = fftOcean->GetSpectrum();
        static_assert(FFTOceanSpectrum::kCascadeCount == 3, "cascadeMeanSquareSlope のカスケード数と一致させる");
        for (uint32_t c = 0; c < FFTOceanSpectrum::kCascadeCount; ++c) {
            input.cascadeMeanSquareSlope[c] = spectrum.GetCascadeMeanSquareSlope(c);
        }
        input.fineWaveMeanSquareSlope = spectrum.GetFineWaveMeanSquareSlope();
        const std::array<float, 3> waveGroupPhase = spectrum.ComputeWaveGroupPhase(time_);
        for (size_t i = 0; i < waveGroupPhase.size(); ++i) {
            input.waveGroupPhase[i] = waveGroupPhase[i];
        }
        return input;
    }
}
