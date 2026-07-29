#include "pch.h"
#include "RayTracingSettingsSection.h"

#include "EngineSystem/EngineSystem.h"
#include "Graphics/RayTracing/RayTracingShadowManager.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Utility/JsonManager/JsonManager.h"

namespace CoreEngine
{
    namespace
    {
        RayTracingShadowManager* GetShadowManager(EngineSystem* engine)
        {
            RenderDomainContext* domain = engine ? engine->GetRenderDomainContext() : nullptr;
            return domain ? domain->GetRayTracingShadowManager() : nullptr;
        }
    }

    void RayTracingSettingsSection::Serialize(json& out) const
    {
        auto* shadowManager = GetShadowManager(engine_);
        if (!shadowManager) {
            return;
        }

        const RayTracingShadowSettings& settings = shadowManager->GetSettings();

        json shadowJson;
        shadowJson["shadowBias"] = settings.shadowBias;
        shadowJson["maxRayDistance"] = settings.maxRayDistance;
        shadowJson["scaleRayDistanceBySunElevation"] = settings.scaleRayDistanceBySunElevation;
        shadowJson["lightRadius"] = settings.lightRadius;
        shadowJson["softShadowSamples"] = settings.softShadowSamples;
        shadowJson["historyAlpha"] = settings.historyAlpha;
        shadowJson["atrousPassCount"] = settings.atrousPassCount;
        shadowJson["denoisePhiDepth"] = settings.denoisePhiDepth;
        shadowJson["disableHistory"] = settings.disableHistory;
        out["shadow"] = shadowJson;
    }

    void RayTracingSettingsSection::Deserialize(const json& in)
    {
        auto* shadowManager = GetShadowManager(engine_);
        if (!shadowManager) {
            return;
        }

        if (!in.contains("shadow")) {
            return;
        }
        const auto& shadowJson = in["shadow"];

        // 現在値を起点に、保存されているフィールドだけ上書きする（欠損キーは現在値を維持）
        RayTracingShadowSettings settings = shadowManager->GetSettings();
        settings.shadowBias = JsonManager::SafeGet(shadowJson, "shadowBias", settings.shadowBias);
        settings.maxRayDistance = JsonManager::SafeGet(shadowJson, "maxRayDistance", settings.maxRayDistance);
        settings.scaleRayDistanceBySunElevation = JsonManager::SafeGet(
            shadowJson, "scaleRayDistanceBySunElevation", settings.scaleRayDistanceBySunElevation);
        settings.lightRadius = JsonManager::SafeGet(shadowJson, "lightRadius", settings.lightRadius);
        settings.softShadowSamples = JsonManager::SafeGet(shadowJson, "softShadowSamples", settings.softShadowSamples);
        settings.historyAlpha = JsonManager::SafeGet(shadowJson, "historyAlpha", settings.historyAlpha);
        settings.atrousPassCount = JsonManager::SafeGet(shadowJson, "atrousPassCount", settings.atrousPassCount);
        settings.denoisePhiDepth = JsonManager::SafeGet(shadowJson, "denoisePhiDepth", settings.denoisePhiDepth);
        settings.disableHistory = JsonManager::SafeGet(shadowJson, "disableHistory", settings.disableHistory);
        shadowManager->SetSettings(settings);
    }
}
