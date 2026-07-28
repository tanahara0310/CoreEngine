#include "pch.h"
#include "Graphics/Water/Simulation/WaterSurfaceModelProvider.h"

namespace CoreEngine
{
    StaticWaterSurfaceModelProvider::StaticWaterSurfaceModelProvider(const char* providerName)
        : providerName_(providerName ? providerName : "StaticWaterSurfaceModelProvider")
    {
    }

    void StaticWaterSurfaceModelProvider::SetSurfaceData(
        const WaterSurfaceData& surfaceData,
        WaterSurfaceSimulationType simulationType)
    {
        surfaceData_ = surfaceData;
        hasSurfaceData_ = true;
        simulationType_ = simulationType;
    }

    void StaticWaterSurfaceModelProvider::ClearSurfaceData()
    {
        surfaceData_ = {};
        hasSurfaceData_ = false;
        simulationType_ = WaterSurfaceSimulationType::Gerstner;
    }

    bool StaticWaterSurfaceModelProvider::TryGetSurfaceData(WaterSurfaceData& outSurfaceData) const
    {
        if (!hasSurfaceData_) {
            return false;
        }

        outSurfaceData = surfaceData_;
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
