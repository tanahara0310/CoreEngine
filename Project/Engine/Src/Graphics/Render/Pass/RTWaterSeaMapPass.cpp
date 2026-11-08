#include "pch.h"
#include "RTWaterSeaMapPass.h"

#include "Graphics/RayTracing/RTScene.h"
#include "Graphics/RayTracing/Debug/RTDebugRegistry.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/Water/RayTracing/WaterSeaMapRayTracingManager.h"

namespace CoreEngine
{
    RT_DEBUG_EFFECT_REGISTER(kSeaMapEffect,
        .id = "RTWaterSeaMap",
        .label = "海の範囲",
        .cvarPrefix = "r.Water.SeaMap",
        .passNames = { "RTWaterSeaMapPass" },
        .stageLabels = { "トレース" },
        .collect = [](const RTDebugQuery& query, std::vector<RTDebugEffectRow>& rows) {
            if (auto* manager = query.domain ? query.domain->GetWaterSeaMapRayTracingManager() : nullptr) {
                rows.push_back({ "ゲームの画面", manager->GetDispatchInfo() });
            }
        });

    void RTWaterSeaMapPass::Execute(const RenderContext& context)
    {
        // 止めたフレームは作り直さない（前に作った地図のまま）
        if (!kSeaMapEffect.IsActive()) {
            return;
        }
        auto* seaMap = context.rtWaterSeaMapManager;
        if (!context.cmdList || !seaMap || !seaMap->IsInitialized()) {
            return;
        }

        // 水面が無いフレームは地図を捨てる（Update が nullptr を受けて捨てる）
        const WaterSurfaceData* surface =
            (context.waterSurfaceState && context.waterSurfaceState->regionValid != 0)
            ? context.waterSurfaceState : nullptr;
        const uint64_t signature = context.rtScene ? context.rtScene->GetStaticSolidSignature() : 0;
        seaMap->Update(context.cmdList, surface, signature);
    }
}
