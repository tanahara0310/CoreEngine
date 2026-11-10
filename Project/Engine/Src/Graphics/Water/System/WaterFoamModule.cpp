#include "pch.h"
#include "WaterFoamModule.h"

#include "Camera/Camera.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/Water/FFT/FFTOceanManager.h"
#include "Graphics/Water/Foam/WaterShoreFoam.h"
#include "Graphics/Water/Foam/WaterWhitecapFoam.h"
#include "Graphics/Water/Surface/WaterRenderResources.h"
#include "Graphics/Water/Surface/WaterSurfaceComponent.h"
#include "Graphics/Water/System/WaterFrameContext.h"
#include "Graphics/Water/System/WaterSettings.h"
#include "Graphics/Water/Wave/WaterWaveModel.h"
#include "Scene/Feature/ISceneFeature.h"

namespace CoreEngine
{
    void WaterFoamModule::ApplySettings(
        const WaterFrameContext& frame, const WaterWaveCapabilities& waves, float fftOceanTime) const
    {
        const WaterFoamSettings& settings = frame.settings.foam;
        WaterSurfaceComponent& waterPlane = frame.waterPlane;
        WaterWhitecapFoam* const whitecap = frame.domain.GetWaterWhitecapFoam();

        // 白波のしきい値は WaterWhitecapFoam が白波の被覆率から較正した値を使う
        float foamBias = WaterFoamDefaults::kBias;
        float foamGain = WaterFoamDefaults::kGain;
        if (whitecap) {
            foamBias = whitecap->GetBias();
            foamGain = whitecap->GetGain();
        }
        waterPlane.SetFoamParameters(
            settings.enabled, foamBias, foamGain, settings.opacity, settings.cascadeWeights, settings.decaySeconds);

        const bool foamEnabled = settings.enabled && waves.foam;
        if (WaterShoreFoam* const shore = frame.domain.GetWaterShoreFoam()) {
            WaterShoreFoam::Settings shoreSettings{};
            shoreSettings.enabled = foamEnabled;
            shoreSettings.decaySeconds = settings.decaySeconds;
            shore->SetSettings(shoreSettings);
        }

        if (!whitecap) {
            return;
        }

        WaterWhitecapFoam::Settings whitecapSettings{};
        whitecapSettings.enabled = foamEnabled;
        whitecapSettings.windSpeed = frame.settings.waves.windSpeed;
        whitecapSettings.whitecapScale = settings.whitecapScale;
        if (const FFTOceanManager* fftOcean = frame.domain.GetFFTOceanManager()) {
            whitecapSettings.windDirection[0] = fftOcean->GetSettings().windDirection[0];
            whitecapSettings.windDirection[1] = fftOcean->GetSettings().windDirection[1];
        }
        whitecapSettings.cascadeWeights[0] = settings.cascadeWeights.x;
        whitecapSettings.cascadeWeights[1] = settings.cascadeWeights.y;
        whitecapSettings.cascadeWeights[2] = settings.cascadeWeights.z;
        whitecapSettings.decaySeconds = settings.decaySeconds;
        whitecap->SetSettings(whitecapSettings);

        // 白波の泡が FFT の時刻までに風下へ流れた距離と、模様を風の向きに伸ばす軸と、模様を動かす時刻
        waterPlane.SetFoamMotion(
            whitecap->ComputeDriftOffset(fftOceanTime), whitecap->ComputeStretchAxis(),
            WaterWhitecapFoam::ComputePatternTime(fftOceanTime));

        // 残りの泡の格子を今のぞいているカメラの周りへ置き直し、同じ範囲を水面へ渡す
        if (const Camera* camera = frame.scene.gameViewCamera3D) {
            const Vector3 cameraPosition = camera->GetPosition();
            whitecap->UpdateWindows(cameraPosition.x, cameraPosition.z, fftOceanTime);
        }
        waterPlane.SetFoamResidue(
            whitecap->GetResidueOrigins(), whitecap->GetResidueInvSizes(), whitecap->GetMeasuredResidueCoverage());
    }

    void WaterFoamModule::ApplyWithoutFoam(const WaterFrameContext& frame) const
    {
        const WaterFoamSettings& settings = frame.settings.foam;
        frame.waterPlane.SetFoamParameters(
            false, WaterFoamDefaults::kBias, WaterFoamDefaults::kGain,
            settings.opacity, settings.cascadeWeights, settings.decaySeconds);
    }

    void WaterFoamModule::Bind(RenderDomainContext& domain, WaterFrameBinding& binding) const
    {
        if (!domain.GetFFTOceanManager()) {
            return;
        }
        if (const WaterWhitecapFoam* whitecap = domain.GetWaterWhitecapFoam(); whitecap && whitecap->IsInitialized()) {
            binding.resources.fftFoamSRV = whitecap->GetSRVHandle();
        }
    }
}
