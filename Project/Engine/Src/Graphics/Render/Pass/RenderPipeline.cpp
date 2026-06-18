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
#include "Graphics/Render/RenderManager.h"

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

    void RenderPipeline::Execute(const RenderContext& context)
    {
        ResetExecutionState();

        for (auto& pass : passes_) {
            ExecutePass(pass.get(), context);
        }
    }

    void RenderPipeline::PrepareFrame(
        const RenderContext& context,
        const std::function<void()>& geometryRenderCallback)
    {
        ResetExecutionState();

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
        ResetExecutionState();

        RenderGraphContext graphContext;
        graphContext.renderContext = &context;
        graphContext.renderPipeline = this;

        renderGraph_.Execute(graphContext);
    }

    void RenderPipeline::ExecutePass(RenderPass* pass, const RenderContext& context)
    {
        if (!pass || !pass->IsEnabled()) {
            return;
        }

        // 前段出力が無効でも明示的に入力を渡し、パス内部に前フレームの入力状態が
        // 残留しないようにする。SceneView では SSAOPass を経由しないため、
        // DeferredLightingPass などが古い入力ハンドルを保持すると表示が汚染される。
        pass->SetInput(previousOutput_);

        pass->Setup(context);
        pass->Execute(context);
        pass->Cleanup(context);
        previousOutput_ = pass->GetOutput();
    }

    void RenderPipeline::ResetExecutionState()
    {
        previousOutput_.Reset();
    }

    void RenderPipeline::Clear()
    {
        passes_.clear();
        previousOutput_.Reset();
    }
}
