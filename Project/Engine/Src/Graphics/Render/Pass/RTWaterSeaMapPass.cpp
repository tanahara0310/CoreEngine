#include "pch.h"
#include "RTWaterSeaMapPass.h"

#include "Graphics/RayTracing/AccelerationStructureManager.h"
#include "Graphics/Water/RayTracing/WaterSeaMapRayTracingManager.h"

namespace CoreEngine
{
    void RTWaterSeaMapPass::Execute(const RenderContext& context)
    {
        auto* seaMap = context.rtWaterSeaMapManager;
        if (!context.cmdList || !seaMap || !seaMap->IsInitialized()) {
            return;
        }

        // 水面が無いフレームは地図を捨てる（Update が nullptr を受けて捨てる）
        const WaterSurfaceData* surface =
            (context.waterSurfaceState && context.waterSurfaceState->regionValid != 0)
            ? context.waterSurfaceState : nullptr;
        const uint64_t signature = context.accelerationStructureManager
            ? context.accelerationStructureManager->GetStaticSolidSignature() : 0;
        seaMap->Update(context.cmdList, surface, signature);
    }
}
