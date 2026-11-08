#include "pch.h"
#include "RTWaterSeaMapPass.h"

#include "Graphics/RayTracing/RTScene.h"
#include "Graphics/RayTracing/Debug/RTDebugRegistry.h"
#include "Graphics/RayTracing/Effect/RTEffectRegistry.h"
#include "Graphics/RayTracing/Effect/RTEffectSet.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/Render/RenderGraph.h"
#include "Graphics/Water/RayTracing/WaterSeaMapRayTracingManager.h"

namespace CoreEngine
{
    RT_EFFECT_REGISTER(WaterSeaMapRayTracingManager,
        .id = "RTWaterSeaMap",
        .group = kWaterRTEffectGroup,
        .passes = {
            RTEffectPassDesc::Of<RTWaterSeaMapPass>(RenderPassPhase::Water, 3),
        })

    RT_DEBUG_EFFECT_REGISTER(kSeaMapEffect,
        .id = "RTWaterSeaMap",
        .label = "海の範囲",
        .cvarPrefix = "r.Water.SeaMap",
        .passNames = { "RTWaterSeaMapPass" },
        .stageLabels = { "トレース" },
        .collect = [](const RTDebugQuery& query, std::vector<RTDebugEffectRow>& rows) {
            if (auto* manager = query.domain ? query.domain->GetRTEffects().Find<WaterSeaMapRayTracingManager>() : nullptr) {
                rows.push_back({ "ゲームの画面", manager->GetDispatchInfo() });
            }
        });

    void RTWaterSeaMapPass::DeclareResources(RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        // 地図は送った後、読み取りの状態のまま置かれる
        builder.Write(FrameBlackboard::WaterSeaMap, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    void RTWaterSeaMapPass::Execute(const RenderContext& context)
    {
        // 止めたフレームは作り直さない（前に作った地図のまま）
        if (!kSeaMapEffect.IsActive()) {
            return;
        }
        auto* seaMap = FindRTEffect<WaterSeaMapRayTracingManager>(context);
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
