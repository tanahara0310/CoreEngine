#include "pch.h"
#include "RTWaterReflectionPass.h"

#include "EngineSystem/Subsystem/RayTracingSubsystem.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RayTracing/Debug/RTDebugRegistry.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/Water/RayTracing/WaterReflectionRayTracingManager.h"
#include "Utility/Logger/Logger.h"
#include "Graphics/Render/RenderGraph.h"

namespace CoreEngine
{
    RT_DEBUG_EFFECT_REGISTER(kReflectionEffect,
        .id = "RTWaterReflection",
        .label = "水の反射",
        .cvarPrefix = nullptr,
        .passNames = { "RTWaterReflectionPass" },
        .stageLabels = { "トレース" },
        .collect = [](const RTDebugQuery& query, std::vector<RTDebugEffectRow>& rows) {
            if (auto* manager = query.domain ? query.domain->GetWaterReflectionRayTracingManager() : nullptr) {
                rows.push_back({ "ゲームの画面", manager->GetDispatchInfo() });
            }
        });

    RT_DEBUG_CHANNEL_REGISTER(
        .effect = "水の反射",
        .stage = "トレース",
        .name = "映り込みの色",
        .description = "水面で跳ね返ったレイが見た色（水面が合成する）",
        .components = { "R", "G", "B", "A" },
        .rangeMin = 0.0f,
        .rangeMax = 1.0f,
        .viewMode = RTDebugViewMode::Color,
        .resolve = [](const RenderContext& context, uint32_t, RTDebugSource& out) {
            return ResolveFrameBlackboardImage(context, FrameBlackboard::RTWaterReflectionColor, out);
        })

    RT_DEBUG_CHANNEL_REGISTER(
        .effect = "水の反射",
        .stage = "トレース",
        .name = "水面の日の当たり方",
        .description = "水面の点から太陽へ撃ったレイの結果。0 = 物の影の中・1 = 日向（水面ではない画素は 1）",
        .components = { "日の当たり方" },
        .rangeMin = 0.0f,
        .rangeMax = 1.0f,
        .viewMode = RTDebugViewMode::Mono,
        .resolve = [](const RenderContext& context, uint32_t, RTDebugSource& out) {
            return ResolveFrameBlackboardImage(context, FrameBlackboard::RTWaterSunVisibility, out);
        })

    void RTWaterReflectionPass::DeclareResources(RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        builder.Read(FrameBlackboard::SceneDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Read(FrameBlackboard::SceneColorSnapshot, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        // 当たった点を照らすときの雲の影（雲を使わないシーンでは登録されない）
        builder.Read(FrameBlackboard::CloudShadowMap, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Write(FrameBlackboard::RTWaterReflectionColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        builder.Write(FrameBlackboard::RTWaterSunVisibility, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    void RTWaterReflectionPass::Execute(const RenderContext& context)
    {
        // 止めたフレームは撃たない（出力を載せないので、水面は映り込みなしで描く）
        if (!kReflectionEffect.IsActive()) {
            return;
        }
        if (!context.rayTracingSubsystem || !context.dxCommon || !context.rtWaterReflectionManager) {
            return;
        }

        if (!context.rtWaterReflectionManager->IsInitialized()) {
            return;
        }

        ID3D12GraphicsCommandList* cmdList = context.cmdList;
        if (!cmdList) {
            return;
        }

        // 水面が存在しない・非表示のフレームはディスパッチしない（RTWaterCausticsPass と同じガード）。
        // 出力を Blackboard へ登録しないため、Water.PS の反射合成も自動的に無効化される。
        if (!context.waterSurfaceState
            || context.waterSurfaceState->regionValid == 0) {
            return;
        }

        const WaterSurfaceData& dispatchSurfaceData = *context.waterSurfaceState;

        context.rayTracingSubsystem->DispatchWaterReflection(
            context,
            context.dxCommon,
            cmdList,
            WaterReflectionRayTracingManager::ViewID::GameView,
            dispatchSurfaceData);

        if (context.frameBlackboard) {
            constexpr auto kView = WaterReflectionRayTracingManager::ViewID::GameView;
            context.frameBlackboard->SetResource(
                FrameBlackboard::RTWaterReflectionColor,
                context.rtWaterReflectionManager->GetReflectionSRVHandle(kView),
                &context.rtWaterReflectionManager->GetReflectionResource(kView));
            context.frameBlackboard->SetResource(
                FrameBlackboard::RTWaterSunVisibility,
                context.rtWaterReflectionManager->GetSunVisibilitySRVHandle(kView),
                &context.rtWaterReflectionManager->GetSunVisibilityResource(kView));
        }
    }
}
