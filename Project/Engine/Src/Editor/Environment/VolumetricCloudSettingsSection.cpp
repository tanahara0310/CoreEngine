#include "pch.h"
#include "VolumetricCloudSettingsSection.h"

#include "EngineSystem/EngineSystem.h"
#include "Editor/Environment/VolumetricCloudEditor.h"
#include "Graphics/Cloud/VolumetricCloudManager.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Utility/JsonManager/JsonManager.h"

namespace CoreEngine
{
    namespace
    {
        VolumetricCloudManager* GetCloudManager(EngineSystem* engine)
        {
            if (!engine || !engine->GetRenderDomainContext()) {
                return nullptr;
            }
            return engine->GetRenderDomainContext()->GetVolumetricCloudManager();
        }
    }

    void VolumetricCloudSettingsSection::Serialize(json& out) const
    {
        auto* manager = GetCloudManager(engine_);
        if (!manager) {
            return;
        }

        const VolumetricCloudParameters& p = manager->GetParameters();

        // 雲層ジオメトリ
        out["layerBottomAltitudeM"] = p.layerBottomAltitudeM;
        out["layerThicknessM"] = p.layerThicknessM;

        // カバレッジ・密度
        out["globalCoverage"] = p.globalCoverage;
        out["densityScale"] = p.densityScale;

        // ノイズスケール
        out["baseNoiseScaleM"] = p.baseNoiseScaleM;
        out["detailNoiseScaleM"] = p.detailNoiseScaleM;
        out["detailErosionStrength"] = p.detailErosionStrength;
        out["weatherMapScaleM"] = p.weatherMapScaleM;

        // 風
        out["windDirX"] = p.windDirX;
        out["windDirZ"] = p.windDirZ;
        out["windSpeedMPerS"] = p.windSpeedMPerS;

        // ライティング
        out["phaseG0"] = p.phaseG0;
        out["phaseG1"] = p.phaseG1;
        out["phaseBlend"] = p.phaseBlend;
        out["ambientIntensity"] = p.ambientIntensity;
        out["beerPowderStrength"] = p.beerPowderStrength;
        out["lightMarchStepM"] = p.lightMarchStepM;

        // 太陽散乱・多重散乱
        out["sunLightScale"] = p.sunLightScale;
        out["msAttenuation"] = p.msAttenuation;
        out["msContribution"] = p.msContribution;
        out["msEccentricity"] = p.msEccentricity;

        // マーチング
        out["earlyExitTransmittance"] = p.earlyExitTransmittance;
        out["maxMarchDistanceM"] = p.maxMarchDistanceM;
        out["maxSteps"] = p.maxSteps;
        out["resolutionDivisor"] = p.resolutionDivisor;

        // ゴッドレイ
        out["godRayEnabled"] = p.godRayEnabled;
        out["godRayIntensity"] = p.godRayIntensity;
        out["godRayMieBoost"] = p.godRayMieBoost;
        out["godRayMaxDistanceM"] = p.godRayMaxDistanceM;
        out["godRayStepCount"] = p.godRayStepCount;
        out["cloudShadowRegionSizeM"] = p.cloudShadowRegionSizeM;

        // 機能トグル・プリセット表示状態
        out["enabled"] = manager->IsEnabled();
        if (editor_) {
            out["activePresetIndex"] = editor_->GetActivePresetIndex();
        }
    }

    void VolumetricCloudSettingsSection::Deserialize(const json& in)
    {
        auto* manager = GetCloudManager(engine_);
        if (!manager) {
            return;
        }

        // 現在値（＝コードデフォルト）を起点に、存在するキーだけ上書きする
        VolumetricCloudParameters& p = manager->GetParametersMutable();

        p.layerBottomAltitudeM = JsonManager::SafeGet(in, "layerBottomAltitudeM", p.layerBottomAltitudeM);
        p.layerThicknessM = JsonManager::SafeGet(in, "layerThicknessM", p.layerThicknessM);
        p.globalCoverage = JsonManager::SafeGet(in, "globalCoverage", p.globalCoverage);
        p.densityScale = JsonManager::SafeGet(in, "densityScale", p.densityScale);
        p.baseNoiseScaleM = JsonManager::SafeGet(in, "baseNoiseScaleM", p.baseNoiseScaleM);
        p.detailNoiseScaleM = JsonManager::SafeGet(in, "detailNoiseScaleM", p.detailNoiseScaleM);
        p.detailErosionStrength = JsonManager::SafeGet(in, "detailErosionStrength", p.detailErosionStrength);
        p.weatherMapScaleM = JsonManager::SafeGet(in, "weatherMapScaleM", p.weatherMapScaleM);
        p.windDirX = JsonManager::SafeGet(in, "windDirX", p.windDirX);
        p.windDirZ = JsonManager::SafeGet(in, "windDirZ", p.windDirZ);
        p.windSpeedMPerS = JsonManager::SafeGet(in, "windSpeedMPerS", p.windSpeedMPerS);
        p.phaseG0 = JsonManager::SafeGet(in, "phaseG0", p.phaseG0);
        p.phaseG1 = JsonManager::SafeGet(in, "phaseG1", p.phaseG1);
        p.phaseBlend = JsonManager::SafeGet(in, "phaseBlend", p.phaseBlend);
        p.ambientIntensity = JsonManager::SafeGet(in, "ambientIntensity", p.ambientIntensity);
        p.beerPowderStrength = JsonManager::SafeGet(in, "beerPowderStrength", p.beerPowderStrength);
        p.lightMarchStepM = JsonManager::SafeGet(in, "lightMarchStepM", p.lightMarchStepM);
        p.sunLightScale = JsonManager::SafeGet(in, "sunLightScale", p.sunLightScale);
        p.msAttenuation = JsonManager::SafeGet(in, "msAttenuation", p.msAttenuation);
        p.msContribution = JsonManager::SafeGet(in, "msContribution", p.msContribution);
        p.msEccentricity = JsonManager::SafeGet(in, "msEccentricity", p.msEccentricity);
        p.earlyExitTransmittance = JsonManager::SafeGet(in, "earlyExitTransmittance", p.earlyExitTransmittance);
        p.maxMarchDistanceM = JsonManager::SafeGet(in, "maxMarchDistanceM", p.maxMarchDistanceM);
        p.maxSteps = JsonManager::SafeGet(in, "maxSteps", p.maxSteps);
        p.resolutionDivisor = JsonManager::SafeGet(in, "resolutionDivisor", p.resolutionDivisor);
        p.godRayEnabled = JsonManager::SafeGet(in, "godRayEnabled", p.godRayEnabled);
        p.godRayIntensity = JsonManager::SafeGet(in, "godRayIntensity", p.godRayIntensity);
        p.godRayMieBoost = JsonManager::SafeGet(in, "godRayMieBoost", p.godRayMieBoost);
        p.godRayMaxDistanceM = JsonManager::SafeGet(in, "godRayMaxDistanceM", p.godRayMaxDistanceM);
        p.godRayStepCount = JsonManager::SafeGet(in, "godRayStepCount", p.godRayStepCount);
        p.cloudShadowRegionSizeM = JsonManager::SafeGet(in, "cloudShadowRegionSizeM", p.cloudShadowRegionSizeM);

        // パラメータは CB へ毎フレーム反映されるため直代入でよい
        // （ノイズテクスチャは起動時 noiseDirty_=true で必ず生成されるため MarkNoiseDirty も不要）

        manager->SetEnabled(JsonManager::SafeGet(in, "enabled", manager->IsEnabled()));

        if (editor_) {
            editor_->SetActivePresetIndex(
                JsonManager::SafeGet(in, "activePresetIndex", editor_->GetActivePresetIndex()));
        }
    }
}
