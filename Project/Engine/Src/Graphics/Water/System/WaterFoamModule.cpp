#include "pch.h"
#include "WaterFoamModule.h"

#include "Camera/Camera.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/Water/FFT/FFTOceanManager.h"
#include "Graphics/Water/Foam/WaterFoamPattern.h"
#include "Graphics/Water/Foam/WaterObjectFoam.h"
#include "Graphics/Water/Foam/WaterShoreFoam.h"
#include "Graphics/Water/Foam/WaterWhitecapFoam.h"
#include "Graphics/Water/Ripple/WaterRippleSimulation.h"
#include "Graphics/Water/Surface/WaterRenderResources.h"
#include "Graphics/Water/Surface/WaterSurfaceComponent.h"
#include "Graphics/Water/System/WaterFrameContext.h"
#include "Graphics/Water/System/WaterSettings.h"
#include "Graphics/Water/WaterCVars.h"
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
        whitecapSettings.bubbleSeconds = settings.bubbleSeconds;
        whitecapSettings.spreadCoefficient = settings.whitecapSpread;
        whitecap->SetSettings(whitecapSettings);

        // 白波の泡が FFT の時刻までに風下へ流れた距離と、模様を風の向きに伸ばす軸と、模様を動かす時刻
        waterPlane.SetFoamMotion(
            whitecap->ComputeDriftOffset(fftOceanTime), whitecap->ComputeStretchAxis(),
            WaterWhitecapFoam::ComputePatternTime(fftOceanTime));

        // 残りの泡の格子を今のぞいているカメラの周りへ置き直す。水面へは、水面が読む書き終わった格子の範囲を渡す
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

    void WaterFoamModule::AdvanceObjectFoam(const WaterFrameContext& frame, float deltaSeconds)
    {
        WaterObjectFoam* const objectFoam = frame.domain.GetWaterObjectFoam();
        if (!objectFoam || !objectFoam->IsInitialized()) {
            return;
        }
        if (const Camera* camera = frame.scene.gameViewCamera3D) {
            const Vector3 cameraPosition = camera->GetPosition();
            const Vector3 backward = camera->GetForward();
            cameraXZ_[0] = cameraPosition.x;
            cameraXZ_[1] = cameraPosition.z;
            viewXZ_[0] = -backward.x;
            viewXZ_[1] = -backward.z;
        }

        const WaterFoamSettings& foam = frame.settings.foam;
        WaterObjectFoam::Settings settings{};
        settings.enabled = foam.enabled;
        settings.flowStrength = foam.objectFlow;
        settings.decaySeconds = foam.decaySeconds;
        settings.bubbleSeconds = foam.bubbleSeconds;
        settings.riseSeconds = foam.riseSeconds;
        // 白波の泡と同じ速さで風下へ流す
        float windDirection[2] = { 1.0f, 0.0f };
        if (const FFTOceanManager* fftOcean = frame.domain.GetFFTOceanManager()) {
            windDirection[0] = fftOcean->GetSettings().windDirection[0];
            windDirection[1] = fftOcean->GetSettings().windDirection[1];
        }
        const float driftSpeed = WaterWhitecapFoam::kWindDriftFactor * (std::max)(frame.settings.waves.windSpeed, 0.0f);
        settings.windDriftVelocity[0] = windDirection[0] * driftSpeed;
        settings.windDriftVelocity[1] = windDirection[1] * driftSpeed;

        const WaterRippleSimulation* const ripple = frame.domain.GetWaterRippleSimulation();
        objectFoam->Advance(
            (ripple && ripple->IsInitialized()) ? ripple->GetFoamSources() : std::span<const Vector4>{},
            deltaSeconds, cameraXZ_[0], cameraXZ_[1], viewXZ_[0], viewXZ_[1], settings);
    }

    void WaterFoamModule::Bind(RenderDomainContext& domain, WaterFrameBinding& binding) const
    {
        BindObjectFoam(domain, binding);
        if (const WaterFoamPattern* pattern = domain.GetWaterFoamPattern();
            pattern && pattern->IsInitialized() && WaterCVars::FoamPatternTexture.Get()) {
            binding.resources.foamNetSRV = pattern->GetNetSRV();
            binding.resources.foamGrainSRV = pattern->GetGrainSRV();
        }
        if (!domain.GetFFTOceanManager()) {
            return;
        }
        if (const WaterWhitecapFoam* whitecap = domain.GetWaterWhitecapFoam(); whitecap && whitecap->IsInitialized()) {
            binding.resources.fftFoamSRV = whitecap->GetSRVHandle();
        }
    }

    void WaterFoamModule::BindObjectFoam(RenderDomainContext& domain, WaterFrameBinding& binding) const
    {
        const WaterObjectFoam* const objectFoam = domain.GetWaterObjectFoam();
        if (!objectFoam || !objectFoam->IsInitialized()) {
            return;
        }
        objectFoam->FillBinding(binding.objectFoam);
        if (binding.objectFoam.enabled != 0) {
            binding.resources.objectFoamSRV = binding.objectFoam.foamSRV;
            binding.resources.objectFoamFlowSRV = binding.objectFoam.flowSRV;
        }
    }
}
