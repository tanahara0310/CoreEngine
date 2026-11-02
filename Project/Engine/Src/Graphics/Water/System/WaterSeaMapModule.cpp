#include "pch.h"
#include "WaterSeaMapModule.h"

#include <algorithm>
#include <cmath>

#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/Water/FFTOceanManager.h"
#include "Graphics/Water/RayTracing/WaterSeaMapRayTracingManager.h"
#include "Graphics/Water/Surface/WaterRenderResources.h"
#include "Graphics/Water/System/WaterSettings.h"

namespace CoreEngine
{
    namespace {
        /// @brief 海の範囲マップで割る波の高さの下限 [m]
        constexpr float kSeaMapMinWaveHeight = 0.05f;

        /// @brief 波の高さ（有義波高）[m]。FFT は風波とうねりの合成、Gerstner は振幅の二乗和から
        float ComputeWaveHeight(RenderDomainContext& domain, const WaterSurfaceData& snapshot)
        {
            if (snapshot.simulationType == kWaterSurfaceModelTypeFFTOcean) {
                const FFTOceanManager* const fftOcean = domain.GetFFTOceanManager();
                if (!fftOcean) {
                    return 0.0f;
                }
                const FFTOceanManager::WaveSummary summary = fftOcean->GetWaveSummary();
                return std::sqrt(summary.windSeaHeight * summary.windSeaHeight
                    + summary.swellHeight * summary.swellHeight);
            }

            float sumSquares = 0.0f;
            for (uint32_t waveIndex = 0; waveIndex < snapshot.activeWaveCount; ++waveIndex) {
                const float amplitude = snapshot.waves[waveIndex].amplitude;
                sumSquares += amplitude * amplitude;
            }
            return 4.0f * std::sqrt(0.5f * sumSquares);
        }
    }

    void WaterSeaMapModule::Capture(
        RenderDomainContext& domain, const WaterSettings& settings, WaterSurfaceData& snapshot) const
    {
        snapshot.seaMap = {};
        const WaterSeaMapRayTracingManager* const seaMapManager = domain.GetWaterSeaMapRayTracingManager();
        if (!seaMapManager || snapshot.regionValid == 0 || !settings.seaMapEnabled) {
            return;
        }
        const WaterSeaMapView& view = seaMapManager->GetView();
        if (!view.valid) {
            return;
        }

        const float waveHeight = ComputeWaveHeight(domain, snapshot);
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
        if (const WaterSeaMapRayTracingManager* const seaMapManager = domain.GetWaterSeaMapRayTracingManager()) {
            binding.resources.seaMapSRV = seaMapManager->GetView().srvHandle;
            binding.seaMap = snapshot.seaMap;
        }
    }
}
