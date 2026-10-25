#include "pch.h"
#include "Editor/Water/WaterEditorFacade.h"

#include "Graphics/Water/Render/WaterRenderFeature.h"
#include "EngineSystem/EngineSystem.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueManager.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueNames.h"

using namespace CoreEngine;

void WaterEditorFacade::Initialize(WaterRenderFeature& runtimeController, EngineSystem& engine) {
    runtimeController_ = &runtimeController;
    engine_ = &engine;
}

WaterEditorRefractionDebug WaterEditorFacade::GetRefractionDebug() const {
    WaterEditorRefractionDebug debug{};
    if (auto* waterRefractionManager = GetWaterRefractionManager()) {
        const WaterRefractionRayTracingSettings& settings = waterRefractionManager->GetSettings();
        debug.available = true;
        debug.displayScale = settings.debugDisplayScale;
        debug.viewMode = static_cast<int>(settings.debugViewMode);
        debug.logEnabled = (settings.debugLogEnabled != 0);
    }
    return debug;
}

void WaterEditorFacade::ApplyRefractionDebug(const WaterEditorRefractionDebug& debug) {
    if (auto* waterRefractionManager = GetWaterRefractionManager()) {
        WaterRefractionRayTracingSettings settings = waterRefractionManager->GetSettings();
        settings.debugDisplayScale = debug.displayScale;
        settings.debugViewMode = static_cast<uint32_t>(debug.viewMode);
        settings.debugLogEnabled = debug.logEnabled ? 1u : 0u;
        waterRefractionManager->SetSettings(settings);
    }
}

WaterEditorCausticsDebug WaterEditorFacade::GetCausticsDebug() const {
    WaterEditorCausticsDebug debug{};
    if (auto* waterCaustics = GetWaterCausticsTechnique()) {
        const WaterCausticsTechnique::Params& params = waterCaustics->GetParams();
        debug.techniqueAvailable = true;
        debug.displayScale = params.debugDisplayScale;
        debug.viewMode = static_cast<int>(params.debugViewMode);
        debug.logEnabled = (params.debugLogEnabled != 0);
    }
    if (auto* rtWaterCausticsManager = GetWaterCausticsManager()) {
        const WaterCausticsRayTracingSettings& settings = rtWaterCausticsManager->GetSettings();
        debug.rtManagerAvailable = true;
        debug.rtDisplayScale = settings.debugDisplayScale;
        debug.rtViewMode = static_cast<int>(settings.debugViewMode);
        debug.rtLogEnabled = (settings.debugLogEnabled != 0);
    }
    return debug;
}

void WaterEditorFacade::ApplyCausticsDebug(const WaterEditorCausticsDebug& debug) {
    if (auto* waterCaustics = GetWaterCausticsTechnique()) {
        WaterCausticsTechnique::Params params = waterCaustics->GetParams();
        params.debugDisplayScale = debug.displayScale;
        params.debugViewMode = static_cast<uint32_t>(debug.viewMode);
        params.debugLogEnabled = debug.logEnabled ? 1 : 0;
        waterCaustics->SetParams(params);
    }
    if (auto* rtWaterCausticsManager = GetWaterCausticsManager()) {
        WaterCausticsRayTracingSettings settings = rtWaterCausticsManager->GetSettings();
        settings.debugDisplayScale = debug.rtDisplayScale;
        settings.debugViewMode = static_cast<uint32_t>(debug.rtViewMode);
        settings.debugLogEnabled = debug.rtLogEnabled ? 1u : 0u;
        rtWaterCausticsManager->SetSettings(settings);
    }
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

WaterCausticsRayTracingManager* WaterEditorFacade::GetWaterCausticsManager() const {
    if (!engine_ || !engine_->GetRenderDomainContext()) {
        return nullptr;
    }
    return engine_->GetRenderDomainContext()->GetWaterCausticsRayTracingManager();
}

WaterCausticsTechnique* WaterEditorFacade::GetWaterCausticsTechnique() const {
    if (!engine_) {
        return nullptr;
    }
    auto* techniqueManager = engine_->GetService<RenderingTechniqueManager>();
    if (!techniqueManager) {
        return nullptr;
    }
    return techniqueManager->GetTechnique<WaterCausticsTechnique>(RenderingTechniqueNames::WaterCaustics);
}
