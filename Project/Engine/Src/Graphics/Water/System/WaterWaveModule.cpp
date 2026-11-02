#include "pch.h"
#include "WaterWaveModule.h"

#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/Water/FFTOceanManager.h"
#include "Graphics/Water/Simulation/FFTOceanSurfaceSimulator.h"
#include "Graphics/Water/Simulation/GerstnerWaterSimulator.h"
#include "Graphics/Water/Surface/WaterRenderResources.h"
#include "Graphics/Water/Surface/WaterSurfaceComponent.h"
#include "Graphics/Water/System/WaterFrameContext.h"

namespace CoreEngine
{
    namespace {
        /// @brief FFT のスペクトルを決める値が同じか（方式の切り替えは見ない）
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

    WaterWaveModule::WaterWaveModule()
        : gerstnerSimulator_(std::make_unique<GerstnerWaterSimulator>())
        , fftOceanSimulator_(std::make_unique<FFTOceanSurfaceSimulator>())
    {
    }

    WaterWaveModule::~WaterWaveModule() = default;

    void WaterWaveModule::Advance(float deltaSeconds, const WaterSurfaceComponent* waterPlane)
    {
        fftOceanSimulator_->AdvanceSimulation(deltaSeconds);
        if (waterPlane && !waterPlane->IsUsingFFTOcean()) {
            gerstnerSimulator_->AdvanceSimulation(deltaSeconds);
        }
    }

    float WaterWaveModule::GetActiveTime(const WaterSurfaceComponent& waterPlane) const
    {
        return GetActiveSimulator(waterPlane).GetElapsedTime();
    }

    float WaterWaveModule::GetFFTOceanTime() const
    {
        return fftOceanSimulator_->GetElapsedTime();
    }

    void WaterWaveModule::ApplyFFTSettings(RenderDomainContext& domain, const WaterWaveSettings& waves)
    {
        FFTOceanManager* const fftOcean = domain.GetFFTOceanManager();
        if (!fftOcean) {
            return;
        }
        if (appliedFFTSettings_ && SameSpectrum(*appliedFFTSettings_, waves)) {
            return;
        }
        appliedFFTSettings_ = waves;

        // 変更の検知・値の整え・GPU の待ち合わせ・スペクトルの作り直しは SetSettings が受け持つ
        FFTOceanManager::Settings settings = fftOcean->GetSettings();
        settings.amplitudeScale = waves.amplitudeScale;
        settings.windDirection[0] = waves.windDirection.x;
        settings.windDirection[1] = waves.windDirection.y;
        settings.windSpeed = waves.windSpeed;
        settings.fetchMeters = waves.fetchMeters;
        settings.choppiness = waves.choppiness;
        settings.activeComponentCount = waves.activeComponentCount;
        settings.swellEnabled = waves.swellEnabled;
        settings.swellHeightMeters = waves.swellHeightMeters;
        settings.swellPeriodSeconds = waves.swellPeriodSeconds;
        settings.swellDirection[0] = waves.swellDirection.x;
        settings.swellDirection[1] = waves.swellDirection.y;
        settings.swellRelativeWidth = waves.swellRelativeWidth;
        settings.swellSpreadExponent = waves.swellSpreadExponent;
        fftOcean->SetSettings(settings);
    }

    void WaterWaveModule::ApplySettings(const WaterFrameContext& frame)
    {
        WaterSurfaceComponent& waterPlane = frame.waterPlane;

        // 波群エンベロープの位相（FFT の時刻までに波のエネルギーが群速度で進んだ分）
        if (const FFTOceanManager* fftOcean = frame.domain.GetFFTOceanManager()) {
            waterPlane.SetWaveGroupPhase(fftOcean->ComputeWaveGroupPhase(GetFFTOceanTime()));
        }

        // 方式の切り替えはシェーダーの作り直しを伴うので、変わったときだけ
        const bool fftEnabled = frame.settings.waves.fftEnabled;
        if (waterPlane.IsUsingFFTOcean() != fftEnabled) {
            waterPlane.SetUseFFTOcean(fftEnabled);
        }

        ApplyFFTSettings(frame.domain, frame.settings.waves);
    }

    void WaterWaveModule::Capture(const WaterSurfaceComponent& waterPlane, WaterSurfaceData& snapshot) const
    {
        WaterSurfaceSimulationInput input{};
        input.waterHeight = waterPlane.GetWaterLevel();
        input.gerstnerConstants = &waterPlane.GetWaterConstants();
        GetActiveSimulator(waterPlane).CaptureSurface(input, snapshot);
    }

    void WaterWaveModule::Bind(RenderDomainContext& domain, WaterFrameBinding& binding) const
    {
        const FFTOceanManager* const fftOcean = domain.GetFFTOceanManager();
        if (!fftOcean) {
            return;
        }
        binding.resources.fftDisplacementSRV = fftOcean->GetDisplacementSRVHandle();
        binding.resources.fftNormalSRV = fftOcean->GetNormalSRVHandle();
        binding.resources.fftJacobianSRV = fftOcean->GetJacobianSRVHandle();
        static_assert(FFTOceanManager::kCascadeCount == 3, "fftCascadeMeanSquareSlope のカスケード数と一致させる");
        for (uint32_t c = 0; c < FFTOceanManager::kCascadeCount; ++c) {
            binding.fftCascadeMeanSquareSlope[c] = fftOcean->GetCascadeMeanSquareSlope(c);
        }
        binding.fineWaveMeanSquareSlope = fftOcean->GetFineWaveMeanSquareSlope();
    }

    const WaterSurfaceSimulator& WaterWaveModule::GetActiveSimulator(const WaterSurfaceComponent& waterPlane) const
    {
        return waterPlane.IsUsingFFTOcean() ? *fftOceanSimulator_ : *gerstnerSimulator_;
    }
}
