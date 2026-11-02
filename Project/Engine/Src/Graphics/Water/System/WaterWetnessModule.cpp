#include "pch.h"
#include "WaterWetnessModule.h"

#include "Graphics/Water/System/WaterSettings.h"

namespace CoreEngine
{
    void WaterWetnessModule::Capture(const WaterSettings& settings, float deltaSeconds, WaterSurfaceData& snapshot)
    {
        if (snapshot.regionValid == 0 || !settings.wetnessEnabled) {
            history_.Clear();
            return;
        }
        history_.Record(snapshot.waterHeight, deltaSeconds, settings.wetnessDrySeconds);
        snapshot.wetness = history_.BuildBinding(settings.wetnessDrySeconds);
    }

    void WaterWetnessModule::Clear()
    {
        history_.Clear();
    }
}
