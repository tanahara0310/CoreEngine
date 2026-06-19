#include "pch.h"
#include "RenderPipeline.h"

#include "BackBufferPass.h"
#include "DeferredLightingPass.h"
#include "GeometryPass.h"
#include "GBufferPass.h"
#include "PostEffectPass.h"
#include "RTShadowPass.h"
#include "SSAOPass.h"
#include "ShadowMapPass.h"
#include "Graphics/Common/Core/DepthStencilManager.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/RenderTarget/BackBufferRenderTarget.h"
#include "Graphics/Render/RenderTarget/OffscreenRenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetNames.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Graphics/Shadow/ShadowMapManager.h"
#include "Scene/IScene.h"

namespace CoreEngine
{
    void RenderPipeline::AddPass(std::unique_ptr<RenderPass> pass)
    {
        if (pass) {
            passes_.push_back(std::move(pass));
        }
    }

    RenderPass* RenderPipeline::GetPass(const std::string& name)
    {
        for (auto& pass : passes_) {
            if (pass->GetName() == name) {
                return pass.get();
            }
        }
        return nullptr;
    }

    void RenderPipeline::PrepareFrame(
        const RenderContext& context,
        const std::function<void()>& geometryRenderCallback)
    {
        RegisterFrameResources(context);

        // View ごとの差分設定を先に反映し、後続の Graph 構築条件を揃える。
        ConfigurePassesForView(context);

        if (auto* geometryPass = GetPass<GeometryPass>()) {
            geometryPass->SetRenderCallback(geometryRenderCallback);

            const bool deferredEnabled = GetPass<DeferredLightingPass>()
                && GetPass<DeferredLightingPass>()->IsEnabled();
            geometryPass->SetClearEnabled(!deferredEnabled);
        }

        if (context.renderManager) {
            const bool deferredEnabled = GetPass<DeferredLightingPass>()
                && GetPass<DeferredLightingPass>()->IsEnabled();
            context.renderManager->SetSkipOpaqueMeshInForwardPass(deferredEnabled);
        }

        BuildRenderGraph(context);
    }

    void RenderPipeline::RegisterFrameResources(const RenderContext& context)
    {
        if (!context.frameBlackboard) {
            return;
        }

        if (context.depthStencilManager) {
            context.frameBlackboard->SetResource(
                FrameBlackboard::SceneDepth,
                context.depthStencilManager->GetDepthSRVHandle(),
                context.depthStencilManager->GetDepthStencilResource(),
                &context.depthStencilManager->GetCurrentState());
        }

        if (context.renderTargetManager) {
            if (RenderTarget* sceneColorTarget = context.renderTargetManager->GetRenderTarget(context.viewSettings.sceneColorTargetName)) {
                D3D12_RESOURCE_STATES* sceneColorState = nullptr;
                if (auto* offscreen = dynamic_cast<OffscreenRenderTarget*>(sceneColorTarget)) {
                    sceneColorState = &offscreen->GetCurrentState();
                }

                context.frameBlackboard->SetResource(
                    FrameBlackboard::SceneColor,
                    sceneColorTarget->GetSRVHandle(),
                    sceneColorTarget->GetResource(),
                    sceneColorState);
            }

            if (RenderTarget* backBufferTarget = context.renderTargetManager->GetRenderTarget("BackBuffer")) {
                D3D12_RESOURCE_STATES* backBufferState = nullptr;
                if (auto* backBuffer = dynamic_cast<BackBufferRenderTarget*>(backBufferTarget)) {
                    backBufferState = &backBuffer->GetCurrentState();
                }

                context.frameBlackboard->SetResource(
                    FrameBlackboard::BackBuffer,
                    backBufferTarget->GetSRVHandle(),
                    backBufferTarget->GetResource(),
                    backBufferState);
            }
        }

        if (context.shadowMapManager) {
            context.frameBlackboard->SetResource(
                FrameBlackboard::ShadowMap,
                context.shadowMapManager->GetSRVHandle(),
                context.shadowMapManager->GetShadowMapResource(),
                &context.shadowMapManager->GetCurrentState());
        }

        if (context.gBufferManager) {
            const struct {
                const char* logicalName;
                GBufferManager::Target target;
            } gBufferTargets[] = {
                { FrameBlackboard::GBufferAlbedoAO, GBufferManager::Target::AlbedoAO },
                { FrameBlackboard::GBufferNormalRoughness, GBufferManager::Target::NormalRoughness },
                { FrameBlackboard::GBufferEmissiveMetallic, GBufferManager::Target::EmissiveMetallic },
                { FrameBlackboard::GBufferWorldPosition, GBufferManager::Target::WorldPosition },
                { FrameBlackboard::GBufferMotionVector, GBufferManager::Target::MotionVector },
            };

            for (const auto& entry : gBufferTargets) {
                context.frameBlackboard->SetResource(
                    entry.logicalName,
                    context.gBufferManager->GetSRVHandle(entry.target),
                    context.gBufferManager->GetResource(entry.target),
                    &context.gBufferManager->GetCurrentState(entry.target));
            }
        }
    }

    void RenderPipeline::ConfigurePassesForView(const RenderContext& context)
    {
        // View ごとに必要なパス有効化と出力先ターゲットを切り替える。
        if (auto* deferredPass = GetPass<DeferredLightingPass>()) {
            deferredPass->SetRenderTargetName(context.viewSettings.sceneColorTargetName);
        }

        if (auto* geometryPass = GetPass<GeometryPass>()) {
            geometryPass->SetRenderTargetName(context.viewSettings.sceneColorTargetName);
        }

        if (auto* ssaoPass = GetPass<SSAOPass>()) {
            ssaoPass->SetEnabled(context.viewSettings.enableSSAO);
        }

        if (auto* rtShadowPass = GetPass<RTShadowPass>()) {
            rtShadowPass->SetEnabled(context.viewSettings.enableRTShadow);
        }

        if (auto* postEffectPass = GetPass<PostEffectPass>()) {
            postEffectPass->SetEnabled(context.viewSettings.enablePostEffect);
        }

        if (auto* backBufferPass = GetPass<BackBufferPass>()) {
            backBufferPass->SetEnabled(context.viewSettings.enableBackBuffer);
        }
    }

    void RenderPipeline::BuildRenderGraph([[maybe_unused]] const RenderContext& context)
    {
        renderGraph_.Reset();
        const RenderViewSettings& viewSettings = context.viewSettings;

        // ShadowMap は主方向ライトの深度を書き出す先行パスとして登録する。
        if (auto* pass = GetPass<ShadowMapPass>()) {
            renderGraph_.AddPass(pass->GetName(), pass, [](RenderGraphBuilder& builder) {
                builder.Write(FrameBlackboard::ShadowMap, D3D12_RESOURCE_STATE_DEPTH_WRITE);
                });
        }

        // GBuffer は各 MRT と SceneDepth を書き込む起点パスとして登録する。
        if (auto* pass = GetPass<GBufferPass>()) {
            renderGraph_.AddPass(pass->GetName(), pass, [](RenderGraphBuilder& builder) {
                builder.Write(FrameBlackboard::SceneDepth, D3D12_RESOURCE_STATE_DEPTH_WRITE);
                builder.Write(FrameBlackboard::GBufferAlbedoAO, D3D12_RESOURCE_STATE_RENDER_TARGET);
                builder.Write(FrameBlackboard::GBufferNormalRoughness, D3D12_RESOURCE_STATE_RENDER_TARGET);
                builder.Write(FrameBlackboard::GBufferEmissiveMetallic, D3D12_RESOURCE_STATE_RENDER_TARGET);
                builder.Write(FrameBlackboard::GBufferWorldPosition, D3D12_RESOURCE_STATE_RENDER_TARGET);
                builder.Write(FrameBlackboard::GBufferMotionVector, D3D12_RESOURCE_STATE_RENDER_TARGET);
                });
        }

        // SSAO は GBuffer と深度を SRV として読み取り、AO バッファへ書き込む。
        if (viewSettings.enableSSAO) {
            if (auto* pass = GetPass<SSAOPass>()) {
                renderGraph_.AddPass(pass->GetName(), pass, [](RenderGraphBuilder& builder) {
                    builder.Read(FrameBlackboard::SceneDepth, D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                    builder.Read(FrameBlackboard::GBufferAlbedoAO, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                    builder.Read(FrameBlackboard::GBufferNormalRoughness, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                    builder.Read(FrameBlackboard::GBufferWorldPosition, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                    builder.Write(FrameBlackboard::SSAO, D3D12_RESOURCE_STATE_RENDER_TARGET);
                    });
            }
        }

        // RT Shadow は GBuffer を読み取り、シャドウ結果を生成する。
        if (viewSettings.enableRTShadow) {
            if (auto* pass = GetPass<RTShadowPass>()) {
                renderGraph_.AddPass(pass->GetName(), pass, [](RenderGraphBuilder& builder) {
                    builder.Read(FrameBlackboard::GBufferWorldPosition, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    builder.Read(FrameBlackboard::GBufferNormalRoughness, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    builder.Read(FrameBlackboard::GBufferMotionVector, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    builder.Write(FrameBlackboard::RTShadowMask, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    });
            }
        }

        // Deferred Lighting は GBuffer / SSAO / ShadowMap / RTShadow / SceneDepth を読み、SceneColor を生成する。
        if (auto* pass = GetPass<DeferredLightingPass>()) {
            renderGraph_.AddPass(pass->GetName(), pass, [viewSettings](RenderGraphBuilder& builder) {
                builder.Read(FrameBlackboard::SceneDepth, D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                builder.Read(FrameBlackboard::GBufferAlbedoAO, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                builder.Read(FrameBlackboard::GBufferNormalRoughness, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                builder.Read(FrameBlackboard::GBufferEmissiveMetallic, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                builder.Read(FrameBlackboard::GBufferWorldPosition, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                if (viewSettings.enableSSAO) {
                    builder.Read(FrameBlackboard::SSAO, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                }
                builder.Read(FrameBlackboard::ShadowMap, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                if (viewSettings.enableRTShadow) {
                    builder.Read(FrameBlackboard::RTShadowMask, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                }
                builder.Write(FrameBlackboard::SceneColor, D3D12_RESOURCE_STATE_RENDER_TARGET);
                });
        }

        // Geometry は SceneColor に上乗せしつつ SceneDepth を read-only DSV/SRV として参照する。
        if (auto* pass = GetPass<GeometryPass>()) {
            renderGraph_.AddPass(pass->GetName(), pass, [](RenderGraphBuilder& builder) {
                builder.Read(FrameBlackboard::SceneColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                builder.Read(FrameBlackboard::SceneDepth, D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                builder.Write(FrameBlackboard::SceneColor, D3D12_RESOURCE_STATE_RENDER_TARGET);
                });
        }

        // PostEffect は SceneColor を読み取り、後段用の SceneColor を再生成する。
        if (viewSettings.enablePostEffect) {
            if (auto* pass = GetPass<PostEffectPass>()) {
                renderGraph_.AddPass(pass->GetName(), pass, [](RenderGraphBuilder& builder) {
                    builder.Read(FrameBlackboard::SceneColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                    builder.Write(FrameBlackboard::SceneColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                    });
            }
        }

        // BackBuffer は最終入力を読み、Present 前のレンダーターゲットとして書き込む。
        if (viewSettings.enableBackBuffer) {
            if (auto* pass = GetPass<BackBufferPass>()) {
                renderGraph_.AddPass(pass->GetName(), pass, [](RenderGraphBuilder& builder) {
                    builder.Read(FrameBlackboard::SceneColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                    builder.Write("BackBuffer", D3D12_RESOURCE_STATE_RENDER_TARGET);
                    });
            }
        }

        renderGraph_.Compile(context);
    }

    void RenderPipeline::ExecuteRenderGraph(const RenderContext& context)
    {
        RenderGraphContext graphContext;
        graphContext.renderContext = &context;

        renderGraph_.Execute(graphContext);
    }

    void RenderPipeline::ExecuteView(
        const RenderContext& context,
        const std::function<void()>& geometryRenderCallback,
        const std::function<void()>& beforeExecute,
        const std::function<void()>& afterExecute)
    {
        PrepareFrame(context, geometryRenderCallback);

        if (beforeExecute) {
            beforeExecute();
        }

        ExecuteRenderGraph(context);

        if (afterExecute) {
            afterExecute();
        }
    }

    ReflectionViewResult RenderPipeline::ExecuteReflectionView(
        const RenderContext& context,
        const std::function<void()>& geometryRenderCallback,
        const std::function<void()>& beforeExecute,
        const std::function<void()>& afterExecute)
    {
        ExecuteView(context, geometryRenderCallback, beforeExecute, afterExecute);
        return BuildReflectionViewResult(context);
    }

    ReflectionViewResult RenderPipeline::BuildReflectionViewResult(const RenderContext& context) const
    {
        ReflectionViewResult result{};

        if (!context.renderTargetManager) {
            return result;
        }

        RenderTarget* reflectionTarget = context.renderTargetManager->GetRenderTarget(RenderTargetNames::ReflectionView);
        if (!reflectionTarget) {
            return result;
        }

        D3D12_RESOURCE_STATES* reflectionState = nullptr;
        if (auto* offscreen = dynamic_cast<OffscreenRenderTarget*>(reflectionTarget)) {
            reflectionState = &offscreen->GetCurrentState();
        }

        if (context.frameBlackboard) {
            context.frameBlackboard->SetResource(
                FrameBlackboard::ReflectionColor,
                reflectionTarget->GetSRVHandle(),
                reflectionTarget->GetResource(),
                reflectionState);
        }

        result.reflectionSrv = reflectionTarget->GetSRVHandle();

        if (context.depthStencilManager) {
            result.sceneDepthSrv = context.depthStencilManager->GetDepthSRVHandle();
        }

        if (RenderTarget* sceneColorTarget = context.renderTargetManager->GetRenderTarget(RenderTargetNames::Offscreen0)) {
            result.sceneColorSrv = sceneColorTarget->GetSRVHandle();
        }

        result.isValid = result.reflectionSrv.ptr != 0;
        return result;
    }

    void RenderPipeline::Clear()
    {
        passes_.clear();
    }
}
