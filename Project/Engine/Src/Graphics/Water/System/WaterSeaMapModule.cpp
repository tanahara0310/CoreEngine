#include "pch.h"
#include "WaterSeaMapModule.h"

#include <algorithm>

#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/Water/RayTracing/WaterSeaMapRayTracingManager.h"
#include "Graphics/Water/Surface/WaterRenderResources.h"
#include "Graphics/Water/System/WaterSettings.h"
#include "Graphics/Water/Wave/WaterWaveModel.h"

namespace CoreEngine
{
    namespace {
        /// @brief 海の範囲マップで割る波の高さの下限 [m]
        constexpr float kSeaMapMinWaveHeight = 0.05f;
    }

    void WaterSeaMapModule::Capture(
        RenderDomainContext& domain, const WaterSettings& settings, const WaterWaveModel& drawnWaves,
        WaterSurfaceData& snapshot) const
    {
        snapshot.seaMap = {};
        const WaterSeaMapRayTracingManager* const seaMapManager = domain.GetRTEffects().Find<WaterSeaMapRayTracingManager>();
        if (!seaMapManager || snapshot.regionValid == 0 || !settings.seaMapEnabled) {
            return;
        }
        const WaterSeaMapView& view = seaMapManager->GetView();
        if (!view.valid) {
            return;
        }

        const float waveHeight = drawnWaves.ComputeSignificantWaveHeight(domain, snapshot);
        WaterSeaMapBinding& seaMap = snapshot.seaMap;
        seaMap.srvHeapIndex = view.srvHeapIndex;
        seaMap.resolution[0] = view.resolution[0];
        seaMap.resolution[1] = view.resolution[1];
        seaMap.originXZ[0] = view.originXZ[0];
        seaMap.originXZ[1] = view.originXZ[1];
        seaMap.texelsPerMeter = view.texelsPerMeter;
        seaMap.depthOffset = snapshot.waterHeight - view.waterHeight;
        seaMap.invWaveHeight = 1.0f / (std::max)(waveHeight, kSeaMapMinWaveHeight);
    }

    void WaterSeaMapModule::Bind(
        RenderDomainContext& domain, const WaterSurfaceData& snapshot, WaterFrameBinding& binding) const
    {
        if (snapshot.seaMap.resolution[0] == 0) {
            return;
        }
        if (const WaterSeaMapRayTracingManager* const seaMapManager = domain.GetRTEffects().Find<WaterSeaMapRayTracingManager>()) {
            binding.resources.seaMapSRV = seaMapManager->GetView().srvHandle;
            binding.seaMap = snapshot.seaMap;
        }
    }
}
