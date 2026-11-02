#include "pch.h"
#include "WaterSettings.h"

#include <algorithm>

#include "Graphics/Water/WaterCVars.h"
#include "Utility/CVar/CVarRegistry.h"

namespace CoreEngine
{
    namespace {
        /// @brief r.Water.* の今の値を WaterSettings へ写す
        WaterSettings ReadFromCVars()
        {
            WaterSettings settings{};

            WaterSurfaceSettings& surface = settings.surface;
            surface.baseColor = WaterCVars::BaseColor.Get();
            surface.roughness = WaterCVars::Roughness.Get();
            surface.fresnelScale = WaterCVars::FresnelScale.Get();
            surface.fresnelF0 = WaterCVars::FresnelF0.Get();
            surface.depthFadeEnabled = WaterCVars::DepthFadeEnabled.Get();
            surface.absorption = WaterCVars::EffectiveAbsorption();
            surface.scattering = WaterCVars::EffectiveScattering();
            surface.refractiveIndex = WaterCVars::CausticsRefractiveIndex.Get();

            WaterWaveSettings& waves = settings.waves;
            waves.fftEnabled = WaterCVars::FFTEnabled.Get();
            waves.amplitudeScale = WaterCVars::FFTAmplitudeScale.Get();
            waves.windDirection = WaterCVars::FFTWindDirection.Get();
            waves.windSpeed = WaterCVars::FFTWindSpeed.Get();
            waves.fetchMeters = WaterCVars::FFTFetchKilometers.Get() * 1000.0f;
            waves.choppiness = WaterCVars::FFTChoppiness.Get();
            waves.activeComponentCount =
                static_cast<uint32_t>((std::max)(WaterCVars::FFTActiveComponentCount.Get(), 1));
            waves.swellEnabled = WaterCVars::SwellEnabled.Get();
            waves.swellHeightMeters = WaterCVars::SwellHeight.Get();
            waves.swellPeriodSeconds = WaterCVars::SwellPeriod.Get();
            waves.swellDirection = WaterCVars::SwellDirection.Get();
            waves.swellRelativeWidth = WaterCVars::SwellRelativeWidth.Get();
            waves.swellSpreadExponent = WaterCVars::SwellSpread.Get();

            WaterFoamSettings& foam = settings.foam;
            foam.enabled = WaterCVars::FoamEnabled.Get();
            foam.whitecapScale = WaterCVars::FoamWhitecapScale.Get();
            foam.opacity = WaterCVars::FoamOpacity.Get();
            foam.cascadeWeights = WaterCVars::FoamCascadeWeights.Get();
            foam.decaySeconds = WaterCVars::FoamDecaySeconds.Get();

            WaterCausticsSettings& caustics = settings.caustics;
            caustics.screenSpace = WaterCVars::CausticsBackend.Get() == 1;
            caustics.intensity = WaterCVars::CausticsIntensity.Get();
            caustics.depthAttenuation = WaterCVars::CausticsDepthAttenuation.Get();
            caustics.curvatureScale = WaterCVars::CausticsCurvatureScale.Get();
            caustics.surfaceSampleRadius = WaterCVars::CausticsSurfaceSampleRadius.Get();
            caustics.receiverNormalStrength = WaterCVars::CausticsReceiverNormalStrength.Get();
            caustics.alignmentPower = WaterCVars::CausticsAlignmentPower.Get();

            settings.refractionMaxOffsetPixels = WaterCVars::RefractionMaxOffsetPixels.Get();
            settings.shoreFineMeshEnabled = WaterCVars::ShoreFineMesh.Get();
            settings.seaMapEnabled = WaterCVars::SeaMapEnabled.Get();
            settings.wetnessEnabled = WaterCVars::WetnessEnabled.Get();
            settings.wetnessDrySeconds = WaterCVars::WetnessDrySeconds.Get();
            return settings;
        }
    }

    const WaterSettings& WaterSettingsSource::Refresh()
    {
        const uint32_t revision = CVarRegistry::Get().GetGlobalRevision();
        if (!loaded_ || revision != cvarRevision_) {
            settings_ = ReadFromCVars();
            cvarRevision_ = revision;
            loaded_ = true;
        }
        return settings_;
    }
}
