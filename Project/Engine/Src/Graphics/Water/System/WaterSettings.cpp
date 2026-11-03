#include "pch.h"
#include "WaterSettings.h"

#include <algorithm>

#include "Graphics/Water/WaterCVars.h"
#include "Graphics/Water/WaterProfile.h"
#include "Scene/Environment/WindCVars.h"

namespace CoreEngine
{
    WaterSettings BuildWaterSettings(const WaterProfile& profile)
    {
        WaterSettings settings{};

        WaterSurfaceSettings& surface = settings.surface;
        surface.baseColor = profile.baseColor;
        surface.roughness = profile.roughness;
        surface.fresnelScale = profile.fresnelScale;
        surface.fresnelF0 = profile.fresnelF0;
        surface.depthFadeEnabled = profile.depthFadeEnabled;
        surface.absorption = profile.EffectiveAbsorption();
        surface.scattering = profile.EffectiveScattering();
        surface.refractiveIndex = profile.refractiveIndex;

        WaterWaveSettings& waves = settings.waves;
        waves.model = profile.WaveModel();
        waves.amplitudeScale = profile.amplitudeScale;
        waves.windDirection = WindCVars::Direction.Get();
        waves.windSpeed = WindCVars::Speed.Get();
        waves.fetchMeters = profile.fetchKm * 1000.0f;
        waves.choppiness = profile.choppiness;
        waves.activeComponentCount = static_cast<uint32_t>((std::max)(profile.componentCount, 1));
        waves.swellEnabled = profile.swellEnabled;
        waves.swellHeightMeters = profile.swellHeight;
        waves.swellPeriodSeconds = profile.swellPeriod;
        waves.swellDirection = profile.swellDirection;
        waves.swellRelativeWidth = profile.swellRelativeWidth;
        waves.swellSpreadExponent = profile.swellSpread;

        WaterFoamSettings& foam = settings.foam;
        foam.enabled = profile.foamEnabled;
        foam.whitecapScale = profile.foamWhitecapScale;
        foam.opacity = profile.foamOpacity;
        foam.cascadeWeights = profile.foamCascadeWeights;
        foam.decaySeconds = profile.foamDecaySeconds;

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
