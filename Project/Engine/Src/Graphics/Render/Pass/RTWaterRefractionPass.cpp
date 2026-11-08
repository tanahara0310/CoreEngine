#include "pch.h"
#include "RTWaterRefractionPass.h"

#include "EngineSystem/Subsystem/RayTracingSubsystem.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RayTracing/Debug/RTDebugRegistry.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/Water/RayTracing/WaterRefractionRayTracingManager.h"
#include "Utility/Logger/Logger.h"
#include "Graphics/Render/RenderGraph.h"

namespace CoreEngine
{
    RT_DEBUG_EFFECT_REGISTER(kRefractionEffect,
        .id = "RTWaterRefraction",
        .label = "水の屈折",
        .cvarPrefix = "r.Water.Refraction",
        .passNames = { "RTWaterRefractionPass" },
        .stageLabels = { "トレース" },
        .collect = [](const RTDebugQuery& query, std::vector<RTDebugEffectRow>& rows) {
            if (auto* manager = query.domain ? query.domain->GetWaterRefractionRayTracingManager() : nullptr) {
                rows.push_back({ "ゲームの画面", manager->GetDispatchInfo() });
            }
        });

    RT_DEBUG_CHANNEL_REGISTER(
        .effect = "水の屈折",
        .stage = "トレース",
        .name = "屈折の色",
        .description = "水面から水の中へ曲がったレイが見た色（水面が合成する）",
        .components = { "R", "G", "B", "A" },
        .rangeMin = 0.0f,
        .rangeMax = 1.0f,
        .viewMode = RTDebugViewMode::Color,
        .resolve = [](const RenderContext& context, uint32_t, RTDebugSource& out) {
            return ResolveFrameBlackboardImage(context, FrameBlackboard::RTWaterRefractionColor, out);
        })

    void RTWaterRefractionPass::DeclareResources(RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        builder.Read(FrameBlackboard::SceneDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Read(FrameBlackboard::SceneColorSnapshot, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        // 当たった点を照らすときの雲の影（雲を使わないシーンでは登録されない）
        builder.Read(FrameBlackboard::CloudShadowMap, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Write(FrameBlackboard::RTWaterRefractionColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    void RTWaterRefractionPass::Execute(const RenderContext& context)
    {
        // 止めたフレームは撃たない（出力を載せないので、水面は屈折なしで描く）
        if (!kRefractionEffect.IsActive()) {
            return;
        }
        // マネージャは水面のあるシーンで作られる（水面の無いシーンでは何もしない）
        if (!context.rtWaterRefractionManager) {
            return;
        }
        if (!context.rayTracingSubsystem || !context.dxCommon) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "RTWaterRefractionPass: skipped. rayTracingSubsystem={} dxCommon={}",
                context.rayTracingSubsystem != nullptr,
                context.dxCommon != nullptr);
            return;
        }

        if (!context.rtWaterRefractionManager->IsInitialized()) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "RTWaterRefractionPass: skipped. manager is not initialized.");
            return;
        }

        // 水面が存在しない・非表示のフレームはディスパッチしない（regionValid=0 = 有効な水域なし）。
        // RTWaterCausticsPass は同じガードを持っていたが、屈折・反射は持っておらず
        // 水面が消えていてもフル解像度で DispatchRays が走っていた。
        if (!context.waterSurfaceState
            || context.waterSurfaceState->regionValid == 0) {
            return;
        }

        ID3D12GraphicsCommandList* cmdList = context.cmdList;
        if (!cmdList) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Command,
                "RTWaterRefractionPass: skipped. command list is null.");
            return;
        }

        // IsEnabledForView が GameView 限定のため、ここに来るのは常に GameView
        const WaterRefractionRayTracingManager::ViewID viewId =
            WaterRefractionRayTracingManager::ViewID::GameView;

        const WaterSurfaceData& dispatchSurfaceData = *context.waterSurfaceState;

        context.rayTracingSubsystem->DispatchWaterRefraction(
            context,
            context.dxCommon,
            cmdList,
            viewId,
            dispatchSurfaceData);

        if (context.frameBlackboard) {
            D3D12_GPU_DESCRIPTOR_HANDLE handle = context.rtWaterRefractionManager->GetRefractionSRVHandle(viewId);
            context.frameBlackboard->SetResource(
                FrameBlackboard::RTWaterRefractionColor,
                handle,
                &context.rtWaterRefractionManager->GetRefractionResource(viewId));
        }

        // 診断ログは UI の「RT屈折ログを有効にする」でのみ出す
        // （以前は毎フレーム 3 本が無条件に流れていた）
        if (context.rtWaterRefractionManager->GetSettings().debugLogEnabled != 0) {
            const auto& diagnostics = context.rtWaterRefractionManager->GetDispatchInfo();
            Logger::GetInstance().Infof(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "RTWaterRefractionPass: status={} viewIndex={} waterHeight={:.3f} size={}x{} blasCount={} outputSRV=0x{:X}",
                ToString(diagnostics.status),
                diagnostics.viewIndex,
                context.rtWaterRefractionManager->GetLastWaterHeight(),
                diagnostics.width,
                diagnostics.height,
                diagnostics.blasCount,
                diagnostics.outputSrvPtr);
        }
    }
}
