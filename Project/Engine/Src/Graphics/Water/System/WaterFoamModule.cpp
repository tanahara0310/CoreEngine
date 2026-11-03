#include "pch.h"
#include "WaterFoamModule.h"

#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/Water/FFT/FFTOceanManager.h"
#include "Graphics/Water/Foam/WaterFoamSystem.h"
#include "Graphics/Water/Surface/WaterRenderResources.h"
#include "Graphics/Water/Surface/WaterSurfaceComponent.h"
#include "Graphics/Water/System/WaterFrameContext.h"
#include "Graphics/Water/System/WaterSettings.h"
#include "Graphics/Water/Wave/WaterWaveModel.h"

namespace CoreEngine
{
    void WaterFoamModule::ApplySettings(
        const WaterFrameContext& frame, const WaterWaveCapabilities& waves, float fftOceanTime) const
    {
        const WaterFoamSettings& settings = frame.settings.foam;
        WaterSurfaceComponent& waterPlane = frame.waterPlane;
        WaterFoamSystem* const foam = frame.domain.GetWaterFoamSystem();

        // 白波のしきい値は WaterFoamSystem が白波の被覆率から較正した値を使う
        float foamBias = WaterFoamDefaults::kBias;
        float foamGain = WaterFoamDefaults::kGain;
        if (foam) {
            foamBias = foam->GetWhitecapBias();
            foamGain = foam->GetWhitecapGain();
        }
        waterPlane.SetFoamParameters(
            settings.enabled, foamBias, foamGain, settings.opacity, settings.cascadeWeights, settings.decaySeconds);

        if (!foam) {
            return;
        }

        WaterFoamSystem::Settings foamSettings{};
        foamSettings.enabled = settings.enabled && waves.foam;
        foamSettings.windSpeed = frame.settings.waves.windSpeed;
        foamSettings.whitecapScale = settings.whitecapScale;
        if (const FFTOceanManager* fftOcean = frame.domain.GetFFTOceanManager()) {
            foamSettings.windDirection[0] = fftOcean->GetSettings().windDirection[0];
            foamSettings.windDirection[1] = fftOcean->GetSettings().windDirection[1];
        }
        foamSettings.cascadeWeights[0] = settings.cascadeWeights.x;
        foamSettings.cascadeWeights[1] = settings.cascadeWeights.y;
        foamSettings.cascadeWeights[2] = settings.cascadeWeights.z;
        foamSettings.decaySeconds = settings.decaySeconds;
        foam->SetSettings(foamSettings);

        // 白波の泡が FFT の時刻までに風下へ流れた距離と、模様を風の向きに伸ばす軸
        waterPlane.SetFoamMotion(foam->ComputeFoamDriftOffset(fftOceanTime), foam->ComputeFoamStretchAxis());
    }

    void WaterFoamModule::Bind(RenderDomainContext& domain, WaterFrameBinding& binding) const
    {
        if (!domain.GetFFTOceanManager()) {
            return;
        }
        if (const WaterFoamSystem* foam = domain.GetWaterFoamSystem(); foam && foam->IsInitialized()) {
            binding.resources.fftFoamSRV = foam->GetWhitecapSRVHandle();
        }
    }
}
