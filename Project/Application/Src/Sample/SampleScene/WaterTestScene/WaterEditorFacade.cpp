#include "pch.h"
#include "WaterEditorFacade.h"

#include "WaterSurfaceRuntimeController.h"
#include "EngineSystem/EngineSystem.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueManager.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueNames.h"

using namespace CoreEngine;

void WaterEditorFacade::Initialize(WaterSurfaceRuntimeController& runtimeController, EngineSystem& engine) {
    runtimeController_ = &runtimeController;
    engine_ = &engine;
}

WaterEditorFFTSettings WaterEditorFacade::GetFFTSettings() const {
    WaterEditorFFTSettings settings{};

    if (runtimeController_ && runtimeController_->GetWaterPlane()) {
        settings.enabled = runtimeController_->GetWaterPlane()->IsUsingFFTOcean();
    }

    if (auto* fftOceanManager = GetFFTOceanManager()) {
        const FFTOceanManager::Settings& managerSettings = fftOceanManager->GetSettings();
        settings.managerAvailable = true;
        settings.initialized = fftOceanManager->IsInitialized();
        settings.resolution = static_cast<int>(managerSettings.resolution);
        settings.patchLength = managerSettings.patchLength;
        settings.amplitudeScale = managerSettings.amplitudeScale;
        settings.windDirection[0] = managerSettings.windDirection[0];
        settings.windDirection[1] = managerSettings.windDirection[1];
        settings.windSpeed = managerSettings.windSpeed;
        settings.choppiness = managerSettings.choppiness;
        settings.activeComponentCount = static_cast<int>(managerSettings.activeComponentCount);
        settings.gravity = managerSettings.gravity;
    }

    return settings;
}

void WaterEditorFacade::ApplyFFTSettings(const WaterEditorFFTSettings& settings) {
    if (runtimeController_ && runtimeController_->GetWaterPlane()) {
        runtimeController_->GetWaterPlane()->SetUseFFTOcean(settings.enabled);
    }

    if (auto* fftOceanManager = GetFFTOceanManager()) {
        FFTOceanManager::Settings managerSettings = fftOceanManager->GetSettings();
        managerSettings.patchLength = settings.patchLength;
        managerSettings.amplitudeScale = settings.amplitudeScale;
        managerSettings.windDirection[0] = settings.windDirection[0];
        managerSettings.windDirection[1] = settings.windDirection[1];
        managerSettings.windSpeed = settings.windSpeed;
        managerSettings.choppiness = settings.choppiness;
        managerSettings.activeComponentCount = static_cast<uint32_t>(settings.activeComponentCount);
        managerSettings.gravity = settings.gravity;
        fftOceanManager->SetSettings(managerSettings);
    }
}

WaterEditorFFTSettings WaterEditorFacade::ResetFFTSettings() {
    if (auto* fftOceanManager = GetFFTOceanManager()) {
        FFTOceanManager::Settings defaultSettings{};
        fftOceanManager->SetSettings(defaultSettings);
    }

    return GetFFTSettings();
}

WaterEditorRayTracingSettings WaterEditorFacade::GetRayTracingSettings() const {
    WaterEditorRayTracingSettings settings{};

    if (auto* waterRefractionManager = GetWaterRefractionManager()) {
        settings.maxRefractionOffsetPixels = waterRefractionManager->GetSettings().maxRefractionOffsetPixels;
    }

    return settings;
}

void WaterEditorFacade::ApplyRayTracingSettings(const WaterEditorRayTracingSettings& settings) {
    if (auto* waterRefractionManager = GetWaterRefractionManager()) {
        WaterRefractionRayTracingSettings managerSettings = waterRefractionManager->GetSettings();
        managerSettings.maxRefractionOffsetPixels = settings.maxRefractionOffsetPixels;
        waterRefractionManager->SetSettings(managerSettings);
    }
}

WaterEditorCausticsSettings WaterEditorFacade::GetCausticsSettings() const {
    WaterEditorCausticsSettings settings{};

    if (auto* waterCaustics = GetWaterCausticsTechnique()) {
        const WaterCausticsTechnique::Params& params = waterCaustics->GetParams();
        settings.techniqueAvailable = true;
        settings.intensity = params.intensity;
        settings.depthAttenuation = params.depthAttenuation;
        settings.curvatureScale = params.curvatureScale;
        settings.surfaceSampleRadius = params.surfaceSampleRadius;
        settings.refractiveIndex = params.refractiveIndex;
        settings.receiverNormalStrength = params.receiverNormalStrength;
        settings.alignmentPower = params.alignmentPower;
        settings.debugDisplayScale = params.debugDisplayScale;
        settings.debugViewMode = static_cast<int>(params.debugViewMode);
        settings.debugLogEnabled = (params.debugLogEnabled != 0);
    }

    return settings;
}

void WaterEditorFacade::ApplyCausticsSettings(const WaterEditorCausticsSettings& settings) {
    if (auto* waterCaustics = GetWaterCausticsTechnique()) {
        WaterCausticsTechnique::Params params = waterCaustics->GetParams();
        params.intensity = settings.intensity;
        params.depthAttenuation = settings.depthAttenuation;
        params.curvatureScale = settings.curvatureScale;
        params.surfaceSampleRadius = settings.surfaceSampleRadius;
        params.refractiveIndex = settings.refractiveIndex;
        params.receiverNormalStrength = settings.receiverNormalStrength;
        params.alignmentPower = settings.alignmentPower;
        params.debugDisplayScale = settings.debugDisplayScale;
        params.debugViewMode = static_cast<uint32_t>(settings.debugViewMode);
        params.debugLogEnabled = settings.debugLogEnabled ? 1u : 0u;
        waterCaustics->SetParams(params);
    }
}

WaterEditorCausticsDiagnostics WaterEditorFacade::GetCausticsDiagnostics() const {
    WaterEditorCausticsDiagnostics diagnostics{};

    if (auto* waterCaustics = GetWaterCausticsTechnique()) {
        const WaterCausticsTechnique::Diagnostics& source = waterCaustics->GetDiagnostics();
        diagnostics.techniqueAvailable = true;
        diagnostics.activeWaveCount = source.activeWaveCount;
        diagnostics.mainLightEnabled = source.mainLightEnabled != 0;
        diagnostics.outputHandle = source.outputHandle;
        diagnostics.worldPositionHandle = source.worldPositionHandle;
        diagnostics.normalHandle = source.normalHandle;
    }

    return diagnostics;
}

FFTOceanManager* WaterEditorFacade::GetFFTOceanManager() const {
    if (!engine_ || !engine_->GetRenderDomainContext()) {
        return nullptr;
    }

    return engine_->GetRenderDomainContext()->GetFFTOceanManager();
}

WaterRefractionRayTracingManager* WaterEditorFacade::GetWaterRefractionManager() const {
    if (!engine_ || !engine_->GetRenderDomainContext()) {
        return nullptr;
    }

    return engine_->GetRenderDomainContext()->GetWaterRefractionRayTracingManager();
}

WaterCausticsTechnique* WaterEditorFacade::GetWaterCausticsTechnique() const {
    if (!engine_) {
        return nullptr;
    }

    auto* techniqueManager = engine_->GetComponent<RenderingTechniqueManager>();
    if (!techniqueManager) {
        return nullptr;
    }

    return techniqueManager->GetTechnique<WaterCausticsTechnique>(RenderingTechniqueNames::WaterCaustics);
}
