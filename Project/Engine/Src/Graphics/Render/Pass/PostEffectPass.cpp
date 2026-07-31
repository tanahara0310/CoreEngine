#include "pch.h"
#include "PostEffectPass.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/PostEffect\Effect\PostEffectBase.h"
#include "Graphics/PostEffect/Effect/PostEffectManager.h"
#include "Graphics/PostEffect/Effect/PostEffectNames.h"
#include "Graphics/PostEffect/Effect/Outline/Outline.h"
#include "Graphics/Render/RenderTarget/OffscreenRenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetNames.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Camera/View/ViewInfo.h"
#include "Graphics/Render/RenderGraph.h"

namespace CoreEngine
{
    namespace {
        constexpr const char* kPostEffectIntermediatePrefix = "PostEffectIntermediate";

        RenderTarget* ResolvePostEffectOutputTarget(const RenderContext& context, const std::string& outputResourceName)
        {
            if (!context.renderTargetManager) {
                return nullptr;
            }

            if (outputResourceName == FrameBlackboard::SceneColor) {
                return context.renderTargetManager->GetRenderTarget(context.viewSettings.sceneColorTargetName);
            }

            if (outputResourceName == FrameBlackboard::PostEffectFinal) {
                return context.renderTargetManager->GetPostEffectFinalTarget();
            }

            if (outputResourceName.rfind(kPostEffectIntermediatePrefix, 0) == 0) {
                const std::string indexText = outputResourceName.substr(std::strlen(kPostEffectIntermediatePrefix));
                if (!indexText.empty()) {
                    const size_t index = static_cast<size_t>(std::stoul(indexText));
                    return context.renderTargetManager->GetPostEffectIntermediateTarget(index);
                }
            }

            return nullptr;
        }
    }

    void PostEffectPass::SetEffect(PostEffectBase* effect, const std::string& effectName)
    {
        effect_ = effect;
        effectName_ = effectName;
    }

    void PostEffectPass::SetInputResourceName(const std::string& resourceName)
    {
        inputResourceName_ = resourceName;
    }

    void PostEffectPass::SetOutputResourceName(const std::string& resourceName)
    {
        outputResourceName_ = resourceName;
    }

    void PostEffectPass::DeclareResources(RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        // effect 未割り当ての placeholder は Graph へ直接登録されないため宣言しない。
        if (!effect_) {
            return;
        }

        const bool isCompute = (effect_->GetExecutionType() == PostEffectExecutionType::Compute);
        const D3D12_RESOURCE_STATES inputState = isCompute
            ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
            : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        const D3D12_RESOURCE_STATES outputState = isCompute
            ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
            : D3D12_RESOURCE_STATE_RENDER_TARGET;

        builder.Read(inputResourceName_, inputState);
        builder.Write(outputResourceName_, outputState);
    }

    void PostEffectPass::Execute(const RenderContext& context)
    {
        if (!context.postEffectManager || !effect_) {
            return;
        }

        D3D12_GPU_DESCRIPTOR_HANDLE inputSrv{};
        if (context.frameBlackboard) {
            context.frameBlackboard->TryGetSrvHandle(inputResourceName_, inputSrv);
        }

        if (inputSrv.ptr == 0) {
#ifdef _DEBUG
            OutputDebugStringA("[PostEffectPass] WARNING: 入力リソースが未解決のためポストエフェクト実行を中断します。\n");
#endif
            return;
        }

        // 注: 以前は context.cameraManager を見ていたが、この項はどこからも代入されておらず
        //     常に nullptr だったため、Outline のクリップ面が一度も更新されていなかった。
        if (effectName_ == PostEffectNames::Outline && context.frameViews) {
            if (auto* outline = dynamic_cast<Outline*>(effect_)) {
                const ViewInfo& view = context.frameViews->GameView();
                if (view.isValid) {
                    outline->SetCameraClipPlanes(view.nearZ, view.farZ);
                }
            }
        }

        if (effect_->GetExecutionType() == PostEffectExecutionType::Compute) {
            if (!context.renderTargetManager) {
                return;
            }

            RenderTarget* outputTarget = ResolvePostEffectOutputTarget(context, outputResourceName_);

            auto* offscreen = dynamic_cast<OffscreenRenderTarget*>(outputTarget);
            if (!offscreen || !context.dxCommon) {
                return;
            }

            auto* cmdList = context.dxCommon->GetCommandList();
            offscreen->BeginCS(cmdList);
            effect_->Dispatch(
                inputSrv,
                offscreen->GetUAVHandle(),
                static_cast<uint32_t>(offscreen->GetWidth()),
                static_cast<uint32_t>(offscreen->GetHeight()));
            offscreen->EndCS(cmdList);

            if (context.frameBlackboard) {
                context.frameBlackboard->SetResource(
                    outputResourceName_,
                    offscreen->GetSRVHandle(),
                    offscreen->GetResource(),
                    &offscreen->GetCurrentState());
            }

            return;
        }

        if (!context.renderTargetManager || !context.dxCommon) {
            return;
        }

        RenderTarget* outputTarget = ResolvePostEffectOutputTarget(context, outputResourceName_);

        if (!outputTarget) {
            return;
        }

        auto* cmdList = context.dxCommon->GetCommandList();
        outputTarget->Begin(cmdList);
        effect_->Draw(inputSrv);
        outputTarget->End(cmdList);

        if (context.frameBlackboard) {
            D3D12_RESOURCE_STATES* stateRef = nullptr;
            if (auto* offscreen = dynamic_cast<OffscreenRenderTarget*>(outputTarget)) {
                stateRef = &offscreen->GetCurrentState();
            }

            context.frameBlackboard->SetResource(
                outputResourceName_,
                outputTarget->GetSRVHandle(),
                outputTarget->GetResource(),
                stateRef);
        }
    }
}
