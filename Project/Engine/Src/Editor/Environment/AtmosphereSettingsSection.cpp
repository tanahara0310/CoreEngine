#include "pch.h"
#include "AtmosphereSettingsSection.h"

#include "EngineSystem/EngineSystem.h"
#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Utility/JsonManager/JsonManager.h"

namespace CoreEngine
{
    namespace
    {
        AtmosphereManager* GetAtmosphereManager(EngineSystem* engine)
        {
            if (!engine || !engine->GetRenderDomainContext()) {
                return nullptr;
            }
            return engine->GetRenderDomainContext()->GetAtmosphereManager();
        }
    }

    // ============================================================
    // AtmosphereSettingsSection（物理パラメータ・エンジン寿命）
    // ============================================================

    void AtmosphereSettingsSection::Serialize(json& out) const
    {
        auto* manager = GetAtmosphereManager(engine_);
        if (!manager) {
            return;
        }

        const AtmosphereParameters& p = manager->GetParameters();

        // ジオメトリ
        out["planetRadius"] = p.planetRadius;
        out["atmosphereTopRadius"] = p.atmosphereTopRadius;

        // レイリー散乱
        out["rayleighScattering"] = JsonManager::Vector3ToJson(p.rayleighScattering);
        out["rayleighScaleHeight"] = p.rayleighScaleHeight;

        // ミー散乱
        out["mieScattering"] = p.mieScattering;
        out["mieAbsorption"] = p.mieAbsorption;
        out["mieScaleHeight"] = p.mieScaleHeight;
        out["miePhaseG"] = p.miePhaseG;

        // オゾン吸収
        out["ozoneAbsorption"] = JsonManager::Vector3ToJson(p.ozoneAbsorption);
        out["ozoneLayerCenter"] = p.ozoneLayerCenter;
        out["ozoneLayerHalfWidth"] = p.ozoneLayerHalfWidth;

        // 地表（groundLevelY はシーン所有の値のため保存しない）
        out["groundAlbedo"] = JsonManager::Vector3ToJson(p.groundAlbedo);

        // 太陽・月ディスク / 星空
        out["sunDiskAngularRadiusDeg"] = p.sunDiskAngularRadiusDeg;
        out["sunDiskLuminanceScale"] = p.sunDiskLuminanceScale;
        out["moonDiskAngularRadiusDeg"] = p.moonDiskAngularRadiusDeg;
        out["moonDiskLuminanceScale"] = p.moonDiskLuminanceScale;
        out["moonPhaseFromSun"] = p.moonPhaseFromSun;
        out["starIntensity"] = p.starIntensity;

        // 多重散乱 / Aerial Perspective
        out["multiScatteringFactor"] = p.multiScatteringFactor;
        out["apKmPerSlice"] = p.apKmPerSlice;

        // マネージャ側トグル
        out["transmittanceOnLight"] = manager->IsTransmittanceOnLightEnabled();
        out["skyAmbientEnabled"] = manager->IsSkyAmbientEnabled();
        out["skyAmbientScale"] = manager->GetSkyAmbientScale();
        out["skySpecularEnabled"] = manager->IsSkySpecularEnabled();
    }

    void AtmosphereSettingsSection::Deserialize(const json& in)
    {
        auto* manager = GetAtmosphereManager(engine_);
        if (!manager) {
            return;
        }

        // 現在値（＝コードデフォルト）を起点に、存在するキーだけ上書きする
        AtmosphereParameters p = manager->GetParameters();

        p.planetRadius = JsonManager::SafeGet(in, "planetRadius", p.planetRadius);
        p.atmosphereTopRadius = JsonManager::SafeGet(in, "atmosphereTopRadius", p.atmosphereTopRadius);
        p.rayleighScattering = JsonManager::SafeGetVector3(in, "rayleighScattering", p.rayleighScattering);
        p.rayleighScaleHeight = JsonManager::SafeGet(in, "rayleighScaleHeight", p.rayleighScaleHeight);
        p.mieScattering = JsonManager::SafeGet(in, "mieScattering", p.mieScattering);
        p.mieAbsorption = JsonManager::SafeGet(in, "mieAbsorption", p.mieAbsorption);
        p.mieScaleHeight = JsonManager::SafeGet(in, "mieScaleHeight", p.mieScaleHeight);
        p.miePhaseG = JsonManager::SafeGet(in, "miePhaseG", p.miePhaseG);
        p.ozoneAbsorption = JsonManager::SafeGetVector3(in, "ozoneAbsorption", p.ozoneAbsorption);
        p.ozoneLayerCenter = JsonManager::SafeGet(in, "ozoneLayerCenter", p.ozoneLayerCenter);
        p.ozoneLayerHalfWidth = JsonManager::SafeGet(in, "ozoneLayerHalfWidth", p.ozoneLayerHalfWidth);
        p.groundAlbedo = JsonManager::SafeGetVector3(in, "groundAlbedo", p.groundAlbedo);
        p.sunDiskAngularRadiusDeg = JsonManager::SafeGet(in, "sunDiskAngularRadiusDeg", p.sunDiskAngularRadiusDeg);
        p.sunDiskLuminanceScale = JsonManager::SafeGet(in, "sunDiskLuminanceScale", p.sunDiskLuminanceScale);
        p.moonDiskAngularRadiusDeg = JsonManager::SafeGet(in, "moonDiskAngularRadiusDeg", p.moonDiskAngularRadiusDeg);
        p.moonDiskLuminanceScale = JsonManager::SafeGet(in, "moonDiskLuminanceScale", p.moonDiskLuminanceScale);
        p.moonPhaseFromSun = JsonManager::SafeGet(in, "moonPhaseFromSun", p.moonPhaseFromSun);
        p.starIntensity = JsonManager::SafeGet(in, "starIntensity", p.starIntensity);
        p.multiScatteringFactor = JsonManager::SafeGet(in, "multiScatteringFactor", p.multiScatteringFactor);
        p.apKmPerSlice = JsonManager::SafeGet(in, "apKmPerSlice", p.apKmPerSlice);

        // SetParameters が LUT 再計算のダーティフラグを立てる（GetParametersMutable 直代入は不可）
        manager->SetParameters(p);

        manager->SetTransmittanceOnLightEnabled(
            JsonManager::SafeGet(in, "transmittanceOnLight", manager->IsTransmittanceOnLightEnabled()));
        manager->SetSkyAmbientEnabled(
            JsonManager::SafeGet(in, "skyAmbientEnabled", manager->IsSkyAmbientEnabled()));
        manager->SetSkyAmbientScale(
            JsonManager::SafeGet(in, "skyAmbientScale", manager->GetSkyAmbientScale()));
        manager->SetSkySpecularEnabled(
            JsonManager::SafeGet(in, "skySpecularEnabled", manager->IsSkySpecularEnabled()));
    }
}
