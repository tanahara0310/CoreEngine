#include "pch.h"
#include "WaterRayTracingModule.h"

#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/Water/RayTracing/WaterReflectionRayTracingManager.h"
#include "Graphics/Water/RayTracing/WaterRefractionRayTracingManager.h"
#include "Graphics/Water/Surface/WaterRenderResources.h"
#include "Graphics/Water/System/WaterFrameContext.h"
#include "Graphics/Water/System/WaterSettings.h"

namespace CoreEngine
{
    void WaterRayTracingModule::ApplySettings(const WaterFrameContext& frame) const
    {
        if (auto* refraction = frame.domain.GetWaterRefractionRayTracingManager()) {
            WaterRefractionRayTracingSettings settings = refraction->GetSettings();
            settings.maxRefractionOffsetPixels = frame.settings.refractionMaxOffsetPixels;
            settings.waterRefractiveIndex = frame.settings.surface.refractiveIndex;
            refraction->SetSettings(settings);
        }
    }

    void WaterRayTracingModule::Bind(RenderDomainContext& domain, WaterFrameBinding& binding) const
    {
        if (auto* refraction = domain.GetWaterRefractionRayTracingManager()) {
            binding.resources.refractionColorSRV = refraction->GetRefractionSRVHandle(
                WaterRefractionRayTracingManager::ViewID::GameView);
        }

        if (auto* reflection = domain.GetWaterReflectionRayTracingManager()) {
            binding.resources.reflectionSRV = reflection->GetReflectionSRVHandle(
                WaterReflectionRayTracingManager::ViewID::GameView);
            // 同じパスが書く水面の日向率（ヤシや岩の影を水面の太陽の項へ掛ける）
            binding.resources.sunVisibilitySRV = reflection->GetSunVisibilitySRVHandle(
                WaterReflectionRayTracingManager::ViewID::GameView);
        }
    }
}
