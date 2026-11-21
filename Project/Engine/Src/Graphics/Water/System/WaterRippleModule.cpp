#include "pch.h"
#include "WaterRippleModule.h"

#include "Camera/Camera.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/Water/Foam/WaterObjectFoam.h"
#include "Graphics/Water/RayTracing/WaterSeaMapRayTracingManager.h"
#include "Graphics/Water/Ripple/WaterRippleSimulation.h"
#include "Graphics/Water/Surface/WaterRenderResources.h"
#include "Graphics/Water/Surface/WaterSurfaceComponent.h"
#include "Graphics/Water/System/WaterFrameContext.h"
#include "Graphics/Water/WaterCVars.h"
#include "Graphics/Water/WaterSurfaceData.h"
#include "Scene/Feature/ISceneFeature.h"

namespace CoreEngine
{
    void WaterRippleModule::Advance(const WaterFrameContext& frame, float deltaSeconds)
    {
        WaterRippleSimulation* const simulation = frame.domain.GetWaterRippleSimulation();
        if (!simulation || !simulation->IsInitialized()) {
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

        WaterRippleSimulation::Settings settings{};
        settings.enabled = WaterCVars::RippleEnabled.Get();
        settings.dampingPerSecond = WaterCVars::RippleDamping.Get();
        simulation->Advance(deltaSeconds, cameraXZ_[0], cameraXZ_[1], viewXZ_[0], viewXZ_[1], settings);
    }

    void WaterRippleModule::Bind(RenderDomainContext& domain, WaterFrameBinding& binding) const
    {
        const WaterRippleSimulation* const simulation = domain.GetWaterRippleSimulation();
        if (!simulation || !simulation->IsInitialized()) {
            return;
        }
        simulation->FillBinding(binding.ripple);
        if (binding.ripple.enabled != 0) {
            binding.resources.rippleSRV = simulation->GetSurfaceSrvHandle();
        }
    }

    void WaterRippleModule::Capture(
        RenderDomainContext& domain, const WaterSurfaceComponent& waterPlane, WaterSurfaceData& out) const
    {
        out.ripple = {};
        const WaterRippleSimulation* const simulation = domain.GetWaterRippleSimulation();
        if (!simulation || !simulation->IsInitialized()) {
            return;
        }
        simulation->FillBinding(out.ripple);
        out.ripple.strength = waterPlane.GetProfile().rippleStrength;
        if (out.ripple.strength <= 0.0f) {
            out.ripple.enabled = 0;
        }
    }

    void WaterRippleModule::CaptureShore(RenderDomainContext& domain, const WaterSurfaceData& snapshot) const
    {
        WaterRippleSimulation* const simulation = domain.GetWaterRippleSimulation();
        WaterObjectFoam* const objectFoam = domain.GetWaterObjectFoam();
        const WaterSeaMapBinding& seaMap = snapshot.seaMap;
        const WaterSeaMapRayTracingManager* const seaMapManager = domain.GetRTEffects().Find<WaterSeaMapRayTracingManager>();
        std::optional<WaterRippleSimulation::ShoreMap> shoreMap;
        if (seaMap.resolution[0] != 0 && seaMap.resolution[1] != 0 && seaMapManager) {
            WaterRippleSimulation::ShoreMap& map = shoreMap.emplace();
            map.srv = seaMapManager->GetView().srvHandle;
            map.resolution[0] = seaMap.resolution[0];
            map.resolution[1] = seaMap.resolution[1];
            map.originXZ[0] = seaMap.originXZ[0];
            map.originXZ[1] = seaMap.originXZ[1];
            map.texelsPerMeter = seaMap.texelsPerMeter;
            map.depthOffset = seaMap.depthOffset;
        }

        // 物の泡の流れはいつも陸で止める。波紋の岸は CVar で切り替える
        if (objectFoam) {
            objectFoam->SetShoreMap(shoreMap);
        }
        if (simulation) {
            simulation->SetShoreMap(WaterCVars::RippleShore.Get() ? shoreMap : std::nullopt);
        }
    }

    void WaterRippleModule::ClearShore(RenderDomainContext& domain)
    {
        if (WaterRippleSimulation* const simulation = domain.GetWaterRippleSimulation()) {
            simulation->SetShoreMap(std::nullopt);
        }
        if (WaterObjectFoam* const objectFoam = domain.GetWaterObjectFoam()) {
            objectFoam->SetShoreMap(std::nullopt);
        }
    }
}
