#include "pch.h"
#include "WaterCausticsModule.h"

#include "EngineSystem/EngineSystem.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/Render/RenderingTechnique/Lighting/WaterCausticsTechnique.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueManager.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueNames.h"
#include "Graphics/Water/RayTracing/WaterCausticsRayTracingManager.h"
#include "Graphics/Water/System/WaterFrameContext.h"
#include "Graphics/Water/System/WaterSettings.h"
#include "Scene/Feature/ISceneFeature.h"

namespace CoreEngine
{
    void WaterCausticsModule::ApplySettings(const WaterFrameContext& frame) const
    {
        const WaterCausticsSettings& caustics = frame.settings.caustics;
        const float refractiveIndex = frame.settings.surface.refractiveIndex;

        // デバッグ用の値は調整画面が受け持つので、見た目の値だけ書き換える
        RenderingTechniqueManager* const techniqueManager =
            frame.scene.engine ? frame.scene.engine->GetService<RenderingTechniqueManager>() : nullptr;
        if (techniqueManager) {
            if (auto* technique = techniqueManager->GetTechnique<WaterCausticsTechnique>(
                    RenderingTechniqueNames::WaterCaustics)) {
                WaterCausticsTechnique::Params params = technique->GetParams();
                params.intensity = caustics.intensity;
                params.depthAttenuation = caustics.depthAttenuation;
                params.curvatureScale = caustics.curvatureScale;
                params.surfaceSampleRadius = caustics.surfaceSampleRadius;
                params.refractiveIndex = refractiveIndex;
                params.receiverNormalStrength = caustics.receiverNormalStrength;
                params.alignmentPower = caustics.alignmentPower;
                technique->SetParams(params);
                technique->SetBackend(caustics.screenSpace
                    ? WaterCausticsTechnique::Backend::ScreenSpace
                    : WaterCausticsTechnique::Backend::RayTracing);
            }
        }

        // 水面の描画と同じ屈折率と波長ごとの吸収係数 σa
        if (auto* rtCaustics = frame.domain.GetWaterCausticsRayTracingManager()) {
            WaterCausticsRayTracingSettings settings = rtCaustics->GetSettings();
            settings.refractiveIndex = refractiveIndex;
            const Vector3& absorption = frame.settings.surface.absorption;
            settings.absorptionCoeff[0] = absorption.x;
            settings.absorptionCoeff[1] = absorption.y;
            settings.absorptionCoeff[2] = absorption.z;
            rtCaustics->SetSettings(settings);
        }
    }
}
