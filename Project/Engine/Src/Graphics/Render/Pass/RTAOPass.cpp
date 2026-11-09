#include "pch.h"
#include "RTAOPass.h"

#include "Camera/View/ViewInfo.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RayTracing/AO/RTAmbientOcclusion.h"
#include "Graphics/RayTracing/Debug/RTDebugRegistry.h"
#include "Graphics/RayTracing/Effect/RTEffectRegistry.h"
#include "Graphics/RayTracing/Effect/RTEffectSet.h"
#include "Graphics/RayTracing/RTScene.h"
#include "Graphics/Render/FrameBlackboard.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/Render/RenderGraph.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueManager.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueNames.h"
#include "Graphics/Render/RenderingTechnique/SSAO/SSAOTechnique.h"

namespace CoreEngine
{
    RT_EFFECT_REGISTER(RTAmbientOcclusion,
        .id = "RTAO",
        .group = nullptr,
        .passes = {
            RTEffectPassDesc::Of<RTAOTracePass>(RenderPassPhase::PreLighting, 16),
            RTEffectPassDesc::Of<RTAOTemporalPass>(RenderPassPhase::PreLighting, 17),
            RTEffectPassDesc::Of<RTAODenoisePass>(RenderPassPhase::PreLighting, 18),
        })

    RT_DEBUG_EFFECT_REGISTER(kRTAOEffect,
        .id = "RTAO",
        .label = "環境光の遮り（RTAO）",
        .cvarPrefix = "r.RTAO",
        .passNames = { "RTAOTrace", "RTAOTemporal", "RTAODenoise" },
        .stageLabels = { "トレース", "時間の蓄積", "デノイズと仕上げ" },
        .collect = [](const RTDebugQuery& query, std::vector<RTDebugEffectRow>& rows) {
            RTAmbientOcclusion* ao = query.domain
                ? query.domain->GetRTEffects().Find<RTAmbientOcclusion>() : nullptr;
            if (!ao || !ao->IsInitialized() || ao->GetDispatchInfo().status == RayTracingDispatchStatus::None) {
                return;
            }
            rows.push_back({ "ゲームの画面", ao->GetDispatchInfo() });
        });

    bool IsRayTracedAOSelected(const RenderContext& context)
    {
        if (!context.viewSettings.enableSSAO || !kRTAOEffect.IsActive() || !RTAmbientOcclusion::IsEnabledByCVar()) {
            return false;
        }
        const RTAmbientOcclusion* ao = FindRTEffect<RTAmbientOcclusion>(context);
        if (!ao || !ao->IsInitialized()) {
            return false;
        }
        // AO そのものの ON/OFF は SSAO の切り替えに従う
        if (context.renderingTechniqueManager) {
            auto* ssao = context.renderingTechniqueManager->GetTechnique<SSAOTechnique>(RenderingTechniqueNames::SSAO);
            if (ssao && !ssao->IsEnabled()) {
                return false;
            }
        }
        return true;
    }

#ifdef CORE_EDITOR
    namespace
    {
        /// @brief RTAO の中間結果の種類
        enum class AODebugImage { Raw, History, Output };

        /// @brief 今フレームに撃った RTAO の中間結果を引く
        bool ResolveAOImage(const RenderContext& context, AODebugImage image, RTDebugSource& out)
        {
            RTAmbientOcclusion* ao = FindRTEffect<RTAmbientOcclusion>(context);
            if (!ao || !ao->IsInitialized() || !ao->IsDispatchedThisFrame()) {
                return false;
            }
            RTDenoiser& denoiser = ao->GetDenoiser();
            constexpr uint32_t channel = RTAmbientOcclusion::kDenoiseChannel;
            switch (image) {
            case AODebugImage::Raw:
                out.resource = &denoiser.GetInput(channel);
                out.srv = denoiser.GetInputSRV(channel);
                break;
            case AODebugImage::History:
                out.resource = &denoiser.GetHistory(channel);
                out.srv = denoiser.GetHistorySRV(channel);
                break;
            default:
                out.resource = &denoiser.GetOutput(channel);
                out.srv = denoiser.GetOutputSRV(channel);
                break;
            }
            return out.resource->IsValid() && out.srv.ptr != 0;
        }
    }
#endif

    RT_DEBUG_CHANNEL_REGISTER(
        .effect = "環境光の遮り（RTAO）",
        .stage = "トレース",
        .name = "生の値",
        .description = "デノイズ前のレイの結果（トレース解像度）。R はざらつくのが正しい。"
            "G は物が現れた直後の所だけ多ければ、そこだけレイを増やす仕組みが効いている",
        .components = { "遮られなかった割合", "撃ったレイの本数" },
        .rangeMin = 0.0f,
        .rangeMax = 1.0f,
        .viewMode = RTDebugViewMode::Mono,
        .resolve = [](const RenderContext& context, uint32_t, RTDebugSource& out) {
            return ResolveAOImage(context, AODebugImage::Raw, out);
        })

    RT_DEBUG_CHANNEL_REGISTER(
        .effect = "環境光の遮り（RTAO）",
        .stage = "時間の蓄積",
        .name = "履歴",
        .description = "前のフレームと重ねた結果（トレース解像度）。ざわつくときは B（重ねた数）を見る。"
            "B が小さい所は履歴を毎フレーム捨てている",
        .components = { "遮られなかった割合", "分散", "重ねたフレーム数", "カメラからの距離" },
        .rangeMin = 0.0f,
        .rangeMax = 1.0f,
        .viewMode = RTDebugViewMode::Mono,
        .resolve = [](const RenderContext& context, uint32_t, RTDebugSource& out) {
            return ResolveAOImage(context, AODebugImage::History, out);
        })

    RT_DEBUG_CHANNEL_REGISTER(
        .effect = "環境光の遮り（RTAO）",
        .stage = "デノイズと仕上げ",
        .name = "AO",
        .description = "ライティングが環境光に掛ける最後の値（画面の解像度）。0 = 遮られている・1 = 開けている",
        .components = { "遮られなかった割合" },
        .rangeMin = 0.0f,
        .rangeMax = 1.0f,
        .viewMode = RTDebugViewMode::Mono,
        .resolve = [](const RenderContext& context, uint32_t, RTDebugSource& out) {
            return ResolveAOImage(context, AODebugImage::Output, out);
        })

    namespace
    {
        /// @brief RTAO の 3 段で共通の入力
        struct AOStageInput {
            RTAmbientOcclusion* ao = nullptr;
            RTDenoiseGuides guides{}; ///< 深度・法線・モーションベクター・投影行列
        };

        /// @brief RTAO の各段の共通の前処理
        /// @return RTAO を選んだフレームで、前提がそろったら true
        bool BuildAOStageInput(const RenderContext& context, AOStageInput& out)
        {
            if (!context.cmdList || !context.dxCommon || !IsRayTracedAOSelected(context)) {
                return false;
            }
            out.ao = FindRTEffect<RTAmbientOcclusion>(context);
            return out.ao && BuildRTDenoiseGuides(context, out.guides);
        }

        /// @brief RTAO の各段が読む深度と法線（と時間の蓄積が読むモーションベクター）を宣言する
        void DeclareAOGuides(RenderGraphBuilder& builder, bool readMotionVector)
        {
            builder.Read(FrameBlackboard::SceneDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            builder.Read(FrameBlackboard::GBufferNormalRoughness, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            if (readMotionVector) {
                builder.Read(FrameBlackboard::GBufferMotionVector, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            }
        }
    }

    void RTAOTracePass::DeclareResources(RenderGraphBuilder& builder, const RenderContext& context)
    {
        if (!IsRayTracedAOSelected(context)) {
            return;
        }
        DeclareAOGuides(builder, false);
    }

    void RTAOTracePass::Execute(const RenderContext& context)
    {
        AOStageInput stage;
        if (!BuildAOStageInput(context, stage)) {
            return;
        }
        const ViewInfo& view = context.frameViews->Get(context.viewSettings.viewType);
        stage.ao->Trace(
            context.cmdList,
            stage.guides,
            view.invViewProjection,
            static_cast<UINT>(context.dxCommon->GetClientWidth()),
            static_cast<UINT>(context.dxCommon->GetClientHeight()),
            context.rtScene ? context.rtScene->GetHitTableIndices() : RTHitTableIndices{});
    }

    void RTAOTemporalPass::DeclareResources(RenderGraphBuilder& builder, const RenderContext& context)
    {
        if (!IsRayTracedAOSelected(context)) {
            return;
        }
        DeclareAOGuides(builder, true);
    }

    void RTAOTemporalPass::Execute(const RenderContext& context)
    {
        AOStageInput stage;
        if (!BuildAOStageInput(context, stage)) {
            return;
        }
        stage.ao->Accumulate(context.cmdList, stage.guides);
    }

    void RTAODenoisePass::DeclareResources(RenderGraphBuilder& builder, const RenderContext& context)
    {
        if (!IsRayTracedAOSelected(context)) {
            return;
        }
        DeclareAOGuides(builder, false);
        // 出力は仕上げの最後にピクセルシェーダの読み取りの状態へ移る
        builder.Write(FrameBlackboard::RTAO, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    void RTAODenoisePass::Execute(const RenderContext& context)
    {
        AOStageInput stage;
        if (!BuildAOStageInput(context, stage)) {
            return;
        }
        stage.ao->Filter(context.cmdList, stage.guides);

        // 撃ったフレームだけ載せる（載っていなければライティングは AO なしで照らす）
        if (stage.ao->IsDispatchedThisFrame() && context.frameBlackboard) {
            context.frameBlackboard->SetResource(
                FrameBlackboard::RTAO,
                stage.ao->GetOutputSRV(),
                &stage.ao->GetOutput());
        }
    }
}
