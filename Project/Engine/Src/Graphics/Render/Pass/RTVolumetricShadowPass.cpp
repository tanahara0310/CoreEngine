#include "pch.h"
#include "RTVolumetricShadowPass.h"

#include "Camera/View/ViewInfo.h"
#include "Graphics/Fog/FogManager.h"
#include "Graphics/Fog/Volumetric/VolumetricFogCVars.h"
#include "Graphics/RayTracing/Debug/RTDebugRegistry.h"
#include "Graphics/RayTracing/Effect/RTEffectRegistry.h"
#include "Graphics/RayTracing/Effect/RTEffectSet.h"
#include "Graphics/RayTracing/RTScene.h"
#include "Graphics/RayTracing/VolumetricShadow/RTVolumetricShadow.h"
#include "Graphics/Render/FrameBlackboard.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/Render/RenderGraph.h"

namespace CoreEngine
{
    RT_EFFECT_REGISTER(RTVolumetricShadow,
        .id = "VolumetricShadow",
        .group = nullptr,
        .passes = {
            RTEffectPassDesc::Of<RTVolumetricShadowPass>(RenderPassPhase::PreLighting, 19),
        })

    RT_DEBUG_EFFECT_REGISTER(kVolumetricShadowEffect,
        .id = "VolumetricShadow",
        .label = "霧の物の影（太陽から見た深さ）",
        .cvarPrefix = "r.Fog.Volumetric",
        .passNames = { "RTVolumetricShadow" },
        .stageLabels = { "トレース" },
        .collect = [](const RTDebugQuery& query, std::vector<RTDebugEffectRow>& rows) {
            RTVolumetricShadow* shadow = query.domain
                ? query.domain->GetRTEffects().Find<RTVolumetricShadow>() : nullptr;
            if (!shadow || !shadow->IsInitialized() || shadow->GetDispatchInfo().status == RayTracingDispatchStatus::None) {
                return;
            }
            rows.push_back({ "ゲームの画面", shadow->GetDispatchInfo() });
        });

    bool IsVolumetricShadowSelected(const RenderContext& context)
    {
        if (context.viewSettings.viewType != RenderViewType::GameView
            || !context.fogManager || !context.fogManager->IsVolumetricRequested()
            || !context.lightManager || !kVolumetricShadowEffect.IsActive()) {
            return false;
        }
        const RTVolumetricShadow* shadow = FindRTEffect<RTVolumetricShadow>(context);
        return shadow && shadow->IsInitialized();
    }

    RT_DEBUG_CHANNEL_REGISTER(
        .effect = "霧の物の影（太陽から見た深さ）",
        .stage = "トレース",
        .name = "太陽から見た深さ",
        .description = "光に垂直な面から光の進む向きへ撃ったレイが、最初に物へ当たった距離 [m]。"
            "画面とは並びが違う（光の向きから見下ろした絵）。何にも当たらなかった所はとても大きい値",
        .components = { "当たった距離 [m]" },
        .rangeMin = 0.0f,
        .rangeMax = 500.0f,
        .viewMode = RTDebugViewMode::Mono,
        .screenSpace = false,
        .resolve = [](const RenderContext& context, uint32_t, RTDebugSource& out) {
            RTVolumetricShadow* shadow = FindRTEffect<RTVolumetricShadow>(context);
            if (!shadow || !shadow->IsInitialized() || !shadow->IsDispatchedThisFrame()) {
                return false;
            }
            out.resource = &shadow->GetDepth();
            out.srv = shadow->GetDepthSrv();
            return out.resource->IsValid() && out.srv.ptr != 0;
        })

    void RTVolumetricShadowPass::DeclareResources(RenderGraphBuilder& builder, const RenderContext& context)
    {
        if (!IsVolumetricShadowSelected(context)) {
            return;
        }
        // 霧の格子が読む前に書き終える
        builder.Write(FrameBlackboard::VolumetricSunDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    void RTVolumetricShadowPass::Execute(const RenderContext& context)
    {
        if (!context.cmdList || !context.frameViews || !IsVolumetricShadowSelected(context)) {
            return;
        }
        RTVolumetricShadow* shadow = FindRTEffect<RTVolumetricShadow>(context);
        const ViewInfo& view = context.frameViews->Get(context.viewSettings.viewType);
        if (!shadow || !view.isValid) {
            return;
        }
        shadow->Trace(
            context.cmdList,
            view,
            *context.lightManager,
            VolumetricFogCVars::Distance.Get(),
            context.rtScene ? context.rtScene->GetHitTableIndices() : RTHitTableIndices{});

        // 書いたフレームだけ載せる（載っていなければ霧の格子は物の影なしで照らす）
        if (shadow->IsDispatchedThisFrame() && context.frameBlackboard) {
            context.frameBlackboard->SetResource(
                FrameBlackboard::VolumetricSunDepth, shadow->GetDepthSrv(), &shadow->GetDepth());
        }
    }
}
