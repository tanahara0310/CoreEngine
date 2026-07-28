#include "pch.h"
#include "RTWaterCausticsPass.h"

#include "EngineSystem/Subsystem/RayTracingSubsystem.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Water/RayTracing/WaterCausticsRayTracingManager.h"
#include "Graphics/Render/RenderingTechnique/Lighting/WaterCausticsTechnique.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueManager.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueNames.h"
#include "Utility/Logger/Logger.h"
#include "Graphics/Render/RenderGraph.h"

namespace CoreEngine
{
    void RTWaterCausticsPass::DeclareResources(RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        builder.Read(FrameBlackboard::SceneDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Write(FrameBlackboard::RTWaterCaustics, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    void RTWaterCausticsPass::Execute(const RenderContext& context)
    {
        if (!context.rayTracingSubsystem || !context.dxCommon || !context.rtWaterCausticsManager) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "RTWaterCausticsPass: skipped. rayTracingSubsystem={} dxCommon={} manager={}",
                context.rayTracingSubsystem != nullptr,
                context.dxCommon != nullptr,
                context.rtWaterCausticsManager != nullptr);
            return;
        }

        // 水面が存在しない・非表示のフレームはディスパッチしない
        // （regionValid=0 = 有効な水域なし）。出力を Blackboard へ登録しないため、
        // DeferredLighting のコースティクス合成と水中ライティングも自動的に無効化される。
        if (!context.waterSurfaceState
            || context.waterSurfaceState->regionValid == 0) {
            return;
        }

        // 生成方式の選択に従う。無効時・スクリーンスペース選択時は DispatchRays ごと省く
        // （出力を Blackboard へ登録しないため、DeferredLighting は自動的に
        //   WaterCausticsPass の出力側を使う）。
        if (context.renderingTechniqueManager) {
            if (auto* caustics = context.renderingTechniqueManager->GetTechnique<WaterCausticsTechnique>(
                    RenderingTechniqueNames::WaterCaustics)) {
                if (!caustics->IsEnabled()
                    || caustics->GetBackend() != WaterCausticsTechnique::Backend::RayTracing) {
                    return;
                }
            }
        }

        if (!context.rtWaterCausticsManager->IsInitialized()) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "RTWaterCausticsPass: skipped. manager is not initialized.");
            return;
        }

        ID3D12GraphicsCommandList* cmdList = context.dxCommon->GetCommandList();
        if (!cmdList) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Command,
                "RTWaterCausticsPass: skipped. command list is null.");
            return;
        }

        const WaterCausticsRayTracingManager::ViewID viewId =
            (context.currentRTShadowViewId == static_cast<uint32_t>(WaterCausticsRayTracingManager::ViewID::ReflectionView))
            ? WaterCausticsRayTracingManager::ViewID::ReflectionView
            : WaterCausticsRayTracingManager::ViewID::GameView;

        const WaterSurfaceData& dispatchSurfaceData = *context.waterSurfaceState;

        context.rayTracingSubsystem->DispatchWaterCaustics(
            context,
            context.dxCommon,
            cmdList,
            viewId,
            dispatchSurfaceData);

        // 診断ログは UI の「RTログを有効にする」でのみ出す（以前は毎フレーム無条件だった）
        if (context.rtWaterCausticsManager->GetSettings().debugLogEnabled != 0) {
            const auto& diagnostics = context.rtWaterCausticsManager->GetLastDiagnostics();
            Logger::GetInstance().Infof(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "RTWaterCausticsPass: status={} viewIndex={} waterHeight={:.3f} activeWaveCount={} size={}x{} blasCount={} outputSRV=0x{:X}",
                WaterCausticsRayTracingManager::ToString(diagnostics.status),
                diagnostics.viewIndex,
                diagnostics.waterHeight,
                diagnostics.activeWaveCount,
                diagnostics.width,
                diagnostics.height,
                diagnostics.blasCount,
                diagnostics.outputSrv);
        }

        if (context.frameBlackboard) {
            D3D12_GPU_DESCRIPTOR_HANDLE handle = context.rtWaterCausticsManager->GetCausticsSRVHandle(viewId);
            ID3D12Resource* resource = context.rtWaterCausticsManager->GetCausticsResource(viewId);
            D3D12_RESOURCE_STATES& currentState = context.rtWaterCausticsManager->GetCausticsCurrentState(viewId);
            context.frameBlackboard->SetResource(
                FrameBlackboard::RTWaterCaustics,
                handle,
                resource,
                &currentState);
        }
    }
}
