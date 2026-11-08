#include "pch.h"
#include "RTWaterCausticsPass.h"

#include "EngineSystem/Subsystem/RayTracingSubsystem.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RayTracing/Debug/RTDebugRegistry.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/Water/RayTracing/WaterCausticsRayTracingManager.h"
#include "Graphics/Render/RenderingTechnique/Lighting/WaterCausticsTechnique.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueManager.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueNames.h"
#include "Utility/Logger/Logger.h"
#include "Graphics/Render/RenderGraph.h"

namespace CoreEngine
{
    bool IsRayTracedWaterCausticsSelected(const RenderContext& context)
    {
        // 水面が存在しない・非表示のフレーム（regionValid=0 = 有効な水域なし）
        if (!context.waterSurfaceState
            || context.waterSurfaceState->regionValid == 0) {
            return false;
        }

        // 生成方式の選択に従う（無効時・スクリーンスペース選択時は false）
        if (context.renderingTechniqueManager) {
            if (auto* caustics = context.renderingTechniqueManager->GetTechnique<WaterCausticsTechnique>(
                    RenderingTechniqueNames::WaterCaustics)) {
                if (!caustics->IsEnabled()
                    || caustics->GetBackend() != WaterCausticsTechnique::Backend::RayTracing) {
                    return false;
                }
            }
        }
        return true;
    }

    RT_DEBUG_EFFECT_REGISTER(kCausticsEffect,
        .id = "RTWaterCaustics",
        .label = "水のコースティクス",
        .cvarPrefix = "r.Water.Caustics",
        .passNames = { "RTWaterCausticsPass" },
        .stageLabels = { "トレース" },
        .collect = [](const RTDebugQuery& query, std::vector<RTDebugEffectRow>& rows) {
            if (auto* manager = query.domain ? query.domain->GetWaterCausticsRayTracingManager() : nullptr) {
                rows.push_back({ "ゲームの画面", manager->GetDispatchInfo() });
            }
        });

    RT_DEBUG_CHANNEL_REGISTER(
        .effect = "水のコースティクス",
        .stage = "トレース",
        .name = "集光",
        .description = "水の底に集まる光（ライティングが足す）。止めたフレームと画面上の水が無いフレームは作らない",
        .components = { "R", "G", "B", "A" },
        .rangeMin = 0.0f,
        .rangeMax = 1.0f,
        .viewMode = RTDebugViewMode::Color,
        .resolve = [](const RenderContext& context, uint32_t, RTDebugSource& out) {
            return ResolveFrameBlackboardImage(context, FrameBlackboard::RTWaterCaustics, out);
        })

    void RTWaterCausticsPass::DeclareResources(RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        builder.Read(FrameBlackboard::SceneDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Read(FrameBlackboard::GBufferNormalRoughness, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Write(FrameBlackboard::RTWaterCaustics, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    void RTWaterCausticsPass::Execute(const RenderContext& context)
    {
        // 止めたフレームは撃たない（出力を載せないので、ライティングはスクリーンスペースの方を使う）
        if (!kCausticsEffect.IsActive()) {
            return;
        }
        // マネージャは水面のあるシーンで作られる（水面の無いシーンでは何もしない）
        if (!context.rtWaterCausticsManager) {
            return;
        }
        if (!context.rayTracingSubsystem || !context.dxCommon) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "RTWaterCausticsPass: skipped. rayTracingSubsystem={} dxCommon={}",
                context.rayTracingSubsystem != nullptr,
                context.dxCommon != nullptr);
            return;
        }

        // 水面が無いフレームと、無効時・スクリーンスペース選択時は DispatchRays ごと省く。
        // 出力を Blackboard へ登録しないため、DeferredLighting のコースティクス合成と
        // 水中ライティングも自動的に無効化される（WaterCausticsPass の出力側を使う）。
        if (!IsRayTracedWaterCausticsSelected(context)) {
            return;
        }

        if (!context.rtWaterCausticsManager->IsInitialized()) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "RTWaterCausticsPass: skipped. manager is not initialized.");
            return;
        }

        ID3D12GraphicsCommandList* cmdList = context.cmdList;
        if (!cmdList) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Command,
                "RTWaterCausticsPass: skipped. command list is null.");
            return;
        }

        // IsEnabledForView が GameView 限定のため、ここに来るのは常に GameView
        const WaterCausticsRayTracingManager::ViewID viewId =
            WaterCausticsRayTracingManager::ViewID::GameView;

        const WaterSurfaceData& dispatchSurfaceData = *context.waterSurfaceState;

        context.rayTracingSubsystem->DispatchWaterCaustics(
            context,
            context.dxCommon,
            cmdList,
            viewId,
            dispatchSurfaceData);

        // 診断ログは UI の「RTログを有効にする」でのみ出す（以前は毎フレーム無条件だった）
        if (context.rtWaterCausticsManager->GetSettings().debugLogEnabled != 0) {
            const auto& diagnostics = context.rtWaterCausticsManager->GetDispatchInfo();
            Logger::GetInstance().Infof(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "RTWaterCausticsPass: status={} viewIndex={} waterHeight={:.3f} activeWaveCount={} size={}x{} blasCount={} outputSRV=0x{:X}",
                ToString(diagnostics.status),
                diagnostics.viewIndex,
                context.rtWaterCausticsManager->GetLastWaterHeight(),
                context.rtWaterCausticsManager->GetLastActiveWaveCount(),
                diagnostics.width,
                diagnostics.height,
                diagnostics.blasCount,
                diagnostics.outputSrvPtr);
        }

        if (context.frameBlackboard) {
            D3D12_GPU_DESCRIPTOR_HANDLE handle = context.rtWaterCausticsManager->GetCausticsSRVHandle(viewId);
            context.frameBlackboard->SetResource(
                FrameBlackboard::RTWaterCaustics,
                handle,
                &context.rtWaterCausticsManager->GetCausticsResource(viewId));
        }
    }
}
