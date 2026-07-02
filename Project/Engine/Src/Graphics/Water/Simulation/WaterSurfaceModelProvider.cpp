#include "pch.h"
#include "Graphics/Water/Simulation/WaterSurfaceModelProvider.h"

namespace CoreEngine
{
    StaticWaterSurfaceModelProvider::StaticWaterSurfaceModelProvider(
        const WaterSurfaceData* surfaceData,
        WaterSurfaceSimulationType simulationType,
        const char* providerName)
    {
        SetSource(surfaceData, simulationType, providerName);
    }

    void StaticWaterSurfaceModelProvider::SetSource(
        const WaterSurfaceData* surfaceData,
        WaterSurfaceSimulationType simulationType,
        const char* providerName)
    {
        surfaceData_ = surfaceData;
        simulationType_ = simulationType;
        providerName_ = providerName ? providerName : "StaticWaterSurfaceModelProvider";
    }

    bool StaticWaterSurfaceModelProvider::TryGetSurfaceData(WaterSurfaceData& outSurfaceData) const
    {
        if (!surfaceData_) {
            return false;
        }

        outSurfaceData = *surfaceData_;
        return true;
    }

    WaterSurfaceSimulationType StaticWaterSurfaceModelProvider::GetSimulationType() const
    {
        return simulationType_;
    }

    const char* StaticWaterSurfaceModelProvider::GetProviderName() const
    {
        return providerName_;
    }
}
