#include "pch.h"
#include "VolumetricFogPass.h"

#include "Camera/View/ViewInfo.h"
#include "Graphics/Cloud/VolumetricCloudManager.h"
#include "Graphics/Fog/FogManager.h"
#include "Graphics/Render/FrameBlackboard.h"
#include "Graphics/Render/RenderGraph.h"
#include "Graphics/Render/RenderTarget/OffscreenRenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"

namespace CoreEngine
{
    namespace
    {
        /// @brief このフレームにボリュメトリックフォグの格子を作るか
        bool IsVolumetricFrame(const RenderContext& context)
        {
            return context.viewSettings.viewType == RenderViewType::GameView
                && context.fogManager && context.fogManager->IsVolumetricRequested()
                && context.cmdList;
        }

        /// @brief 格子へ書く宣言（3 本のパスと FogPass の順を依存で決める）。格子を作らないフレームは何も宣言しない
        void DeclareVolumeWrite(RenderGraphBuilder& builder, const RenderContext& context)
        {
            if (context.fogManager && context.fogManager->IsVolumetricRequested()) {
                builder.Write(FrameBlackboard::VolumetricFog, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            }
        }

        /// @brief 雲の影のマップを読む宣言（雲が無いシーンでは登録されない）
        void DeclareCloudShadowRead(RenderGraphBuilder& builder, const RenderContext& context)
        {
            if (context.fogManager && context.fogManager->IsVolumetricRequested()) {
                builder.Read(FrameBlackboard::CloudShadowMap, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            }
        }

        /// @brief 今フレームの雲の影のマップと範囲を入力へ詰める（雲が無ければ空のまま）
        void FillCloudShadowInput(const RenderContext& context, VolumetricFogFrameInput& input)
        {
            const VolumetricCloudManager* clouds = context.volumetricCloudManager;
            if (!clouds || !clouds->AreCloudsActive() || !context.frameBlackboard) {
                return;
            }
            D3D12_GPU_DESCRIPTOR_HANDLE cloudShadowMap{};
            if (!context.frameBlackboard->TryGetSrvHandle(FrameBlackboard::CloudShadowMap, cloudShadowMap)) {
                return;
            }
            input.cloudShadowMapSrv = cloudShadowMap;
            input.cloudShadow = &clouds->GetCloudShadowConstants();
        }
    }

    void VolumetricFogInjectPass::DeclareResources(
        RenderGraphBuilder& builder, const RenderContext& context)
    {
        DeclareCloudShadowRead(builder, context);
        DeclareVolumeWrite(builder, context);
    }

    void VolumetricFogInjectPass::Execute(const RenderContext& context)
    {
        if (!IsVolumetricFrame(context) || !context.frameViews || !context.renderTargetManager) {
            return;
        }

        const ViewInfo& view = context.frameViews->Get(context.viewSettings.viewType);
        auto* sceneColorTarget = dynamic_cast<OffscreenRenderTarget*>(
            context.renderTargetManager->GetRenderTarget(context.viewSettings.sceneColorTargetName));
        if (!view.isValid || !sceneColorTarget || !sceneColorTarget->GetResource()) {
            return;
        }
        const D3D12_RESOURCE_DESC sceneDesc = sceneColorTarget->Resource().Desc();

        FogManager& fog = *context.fogManager;
        VolumetricFogFrameInput input{};
        input.view = &view;
        input.screenWidth = static_cast<uint32_t>(sceneDesc.Width);
        input.screenHeight = sceneDesc.Height;
        input.lightManager = context.lightManager;
        input.atmosphere = context.atmosphereManager;
        input.fog = &fog.GetSettings();
        input.fallbackAmbient = fog.GetFallbackAmbient();
        FillCloudShadowInput(context, input);

        VolumetricFog& volumetric = fog.GetVolumetric();
        if (!volumetric.PrepareFrame(input)) {
            return;
        }
        volumetric.DispatchInject(context.cmdList);

        // 後ろのパスと FogPass のバリアが格子を見つけられるよう、積み上げ先をここで載せる
        if (context.frameBlackboard) {
            context.frameBlackboard->SetResource(FrameBlackboard::VolumetricFog,
                volumetric.GetIntegratedSrv(), &volumetric.GetIntegratedResource());
        }
    }

    void VolumetricFogLightScatteringPass::DeclareResources(
        RenderGraphBuilder& builder, const RenderContext& context)
    {
        DeclareCloudShadowRead(builder, context);
        DeclareVolumeWrite(builder, context);
    }

    void VolumetricFogLightScatteringPass::Execute(const RenderContext& context)
    {
        if (!IsVolumetricFrame(context)) {
            return;
        }
        context.fogManager->GetVolumetric().DispatchLightScattering(context.cmdList);
    }

    void VolumetricFogIntegratePass::DeclareResources(
        RenderGraphBuilder& builder, const RenderContext& context)
    {
        DeclareVolumeWrite(builder, context);
    }

    void VolumetricFogIntegratePass::Execute(const RenderContext& context)
    {
        if (!IsVolumetricFrame(context)) {
            return;
        }
        context.fogManager->GetVolumetric().DispatchIntegrate(context.cmdList);
    }
}
