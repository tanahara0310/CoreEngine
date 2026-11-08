#include "pch.h"
#include "RTWaterCausticsPass.h"

#include "Graphics/Light/LightManager.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RayTracing/Debug/RTDebugRegistry.h"
#include "Graphics/RayTracing/Effect/RTEffectRegistry.h"
#include "Graphics/RayTracing/Effect/RTEffectSet.h"
#include "Graphics/RayTracing/RTScene.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/Water/RayTracing/WaterCausticsRayTracingManager.h"
#include "Graphics/Render/RenderingTechnique/Lighting/WaterCausticsTechnique.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueManager.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueNames.h"
#include "Math/MathCore.h"
#include "Utility/Logger/Logger.h"
#include "Graphics/Render/RenderGraph.h"

namespace CoreEngine
{
    RT_EFFECT_REGISTER(WaterCausticsRayTracingManager,
        .id = "RTWaterCaustics",
        .group = kWaterRTEffectGroup,
        .passes = {
            // 実行順の都合で PreLighting だが、コストの分類は水面
            RTEffectPassDesc::Of<RTWaterCausticsPass>(RenderPassPhase::PreLighting, 20, GpuTimingCategory::Water),
        })

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
            if (auto* manager = query.domain ? query.domain->GetRTEffects().Find<WaterCausticsRayTracingManager>() : nullptr) {
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

    namespace {
        /// @brief シーンのメインライト（0 番）の方向・色・強度・有効フラグを集める
        WaterCausticsRayTracingManager::LightInput BuildCausticsLightInput(const RenderContext& context)
        {
            WaterCausticsRayTracingManager::LightInput lightInput{};
            lightInput.enabled = false;
            if (!context.lightManager) {
                return lightInput;
            }
            const Light* mainLight = context.lightManager->GetDirectionalLight(0);
            if (!mainLight || !mainLight->enabled) {
                return lightInput;
            }
            lightInput.direction = CoreEngine::Normalize(mainLight->direction);
            // 大気透過率適用済みの実効色（DeferredLighting・SS版コースティクスと同一基準）。
            // 生の color を渡すと日没時に置換前後で輝度が食い違い水面線が不連続になる
            const Vector3 effectiveColor = context.lightManager->GetEffectiveLightColorRGB(*mainLight);
            lightInput.color = { effectiveColor.x, effectiveColor.y, effectiveColor.z };
            // オーサリングは照度 [lx]。シェーダーが期待する従来単位へ換算する
            lightInput.intensity = LightUnits::LuxToShader(mainLight->intensity);
            lightInput.enabled = true;
            return lightInput;
        }
    }

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
        // 効果は水面のあるシーンで作られる（水面の無いシーンでは何もしない）
        WaterCausticsRayTracingManager* caustics = FindRTEffect<WaterCausticsRayTracingManager>(context);
        if (!caustics) {
            return;
        }
        if (!context.dxCommon) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "RTWaterCausticsPass: skipped. dxCommon is null.");
            return;
        }

        // 水面が無いフレームと、無効時・スクリーンスペース選択時は DispatchRays ごと省く。
        // 出力を Blackboard へ登録しないため、DeferredLighting のコースティクス合成と
        // 水中ライティングも自動的に無効化される（WaterCausticsPass の出力側を使う）。
        if (!IsRayTracedWaterCausticsSelected(context)) {
            return;
        }

        if (!caustics->IsInitialized()) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "RTWaterCausticsPass: skipped. manager is not initialized.");
            return;
        }

        if (!context.cmdList) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Command,
                "RTWaterCausticsPass: skipped. command list is null.");
            return;
        }

        // IsEnabledForView が GameView 限定のため、ここに来るのは常に GameView
        const WaterCausticsRayTracingManager::ViewID viewId =
            WaterCausticsRayTracingManager::ViewID::GameView;

        const WaterSurfaceData& surfaceData = *context.waterSurfaceState;

        // コースティクスは屈折・反射と違い SceneColor へ再投影しないので必須ではない
        WaterRayTracingPassBase::FrameInput frame;
        if (WaterRayTracingPassBase::BuildFrameInput(context, surfaceData, "water caustics", false, frame)) {
            caustics->Dispatch(
                context.cmdList,
                frame.sceneDepthSRV,
                context.gBufferManager->GetSRVHandle(GBufferManager::Target::NormalRoughness),
                BuildCausticsLightInput(context),
                surfaceData,
                frame.fftOceanInput,
                MathCore::Matrix::Inverse(frame.viewProjection),
                frame.width,
                frame.height,
                viewId,
                context.rtScene ? context.rtScene->GetHitTableIndices() : RTHitTableIndices{});
        }

        // 診断ログは UI の「RTログを有効にする」でのみ出す
        if (caustics->GetSettings().debugLogEnabled != 0) {
            const auto& diagnostics = caustics->GetDispatchInfo();
            Logger::GetInstance().Infof(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "RTWaterCausticsPass: status={} viewIndex={} waterHeight={:.3f} activeWaveCount={} size={}x{} blasCount={} outputSRV=0x{:X}",
                ToString(diagnostics.status),
                diagnostics.viewIndex,
                caustics->GetLastWaterHeight(),
                caustics->GetLastActiveWaveCount(),
                diagnostics.width,
                diagnostics.height,
                diagnostics.blasCount,
                diagnostics.outputSrvPtr);
        }

        if (context.frameBlackboard) {
            D3D12_GPU_DESCRIPTOR_HANDLE handle = caustics->GetCausticsSRVHandle(viewId);
            context.frameBlackboard->SetResource(
                FrameBlackboard::RTWaterCaustics,
                handle,
                &caustics->GetCausticsResource(viewId));
        }
    }
}
