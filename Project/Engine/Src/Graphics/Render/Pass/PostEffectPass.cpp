#include "pch.h"
#include "PostEffectPass.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/Render/RenderTarget/OffscreenRenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetNames.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Graphics/Render/RenderGraph.h"
#include "Utility/Logger/Logger.h"
#include <unordered_set>

namespace CoreEngine
{
    namespace {
        /// @brief 論理リソース名から書き込み先の実ターゲットを引く
        /// @details SceneColor だけは View ごとに実体が変わるため別扱い。
        ///          それ以外（PostEffectFinal / PostEffectIntermediateN / PostEffectTransientN）は
        ///          論理名と登録名が一致しているのでそのまま引ける。
        RenderTarget* ResolvePostEffectOutputTarget(const RenderContext& context, const std::string& outputResourceName)
        {
            if (!context.renderTargetManager || outputResourceName.empty()) {
                return nullptr;
            }

            if (outputResourceName == FrameBlackboard::SceneColor) {
                return context.renderTargetManager->GetRenderTarget(context.viewSettings.sceneColorTargetName);
            }

            return context.renderTargetManager->GetRenderTarget(outputResourceName);
        }
    }

    void PostEffectPass::SetStep(PostEffectBase* effect, const std::string& effectName, const PostEffectStep& step)
    {
        effect_     = effect;
        effectName_ = effectName;
        step_       = step;
        primaryInputName_ = step.reads.empty() ? std::string() : step.reads.front();
    }

    void PostEffectPass::DeclareResources(RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        // step 未設定の placeholder は Graph へ直接登録されないため宣言しない。
        if (!effect_ || step_.write.empty()) {
            return;
        }

        const bool isCompute = (step_.executionType == PostEffectExecutionType::Compute);
        const D3D12_RESOURCE_STATES inputState = isCompute
            ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
            : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        const D3D12_RESOURCE_STATES outputState = isCompute
            ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
            : D3D12_RESOURCE_STATE_RENDER_TARGET;

        for (const std::string& readName : step_.reads) {
            builder.Read(readName, inputState);
        }
        builder.Write(step_.write, outputState);
    }

    bool PostEffectPass::ResolveExtraInputs(const RenderContext& context)
    {
        effect_->ClearResolvedExtraInputs();

        std::vector<PostEffectInputBinding> bindings;
        effect_->DeclareExtraInputs(bindings);

        for (const PostEffectInputBinding& binding : bindings) {
            D3D12_GPU_DESCRIPTOR_HANDLE srv{};
            if (binding.logicalName && context.frameBlackboard) {
                context.frameBlackboard->TryGetSrvHandle(binding.logicalName, srv);
            }

            if (srv.ptr == 0 && binding.required) {
                // ここへ来るのは異常系。必須入力の有無は RenderPipeline::AppendPostEffectPasses が
                // パス生成前に判定しており、無いエフェクトはチェーンから外れているはず。
                // 到達した場合は「グラフ構築後に Blackboard が変わった」ことを意味する。
                static std::unordered_set<std::string> warned;
                std::string key = effectName_ + "/" + (binding.logicalName ? binding.logicalName : "(null)");
                if (warned.insert(key).second) {
                    Logger::GetInstance().Errorf(LogCategory::Graphics,
                        "[PostEffect] {} の入力 {} がグラフ構築後に失われました（チェーン構築時の判定と不整合）",
                        effectName_, binding.logicalName ? binding.logicalName : "(null)");
                }
                return false;
            }

            effect_->SetResolvedExtraInput(binding.slot, srv);
        }
        return true;
    }

    void PostEffectPass::Execute(const RenderContext& context)
    {
        // ステップの中身（record ラムダ）はエフェクト側が組み立て済み。
        // ここは入力 SRV と出力ターゲットを解決して渡すだけに徹する
        if (!effect_ || step_.write.empty() || !step_.record || !context.renderTargetManager) {
            return;
        }

        PostEffectPassContext passContext;
        passContext.cmdList = context.cmdList;
        passContext.reads.reserve(step_.reads.size());

        // 入力 SRV が 1 つでも未解決なら、黒画面やクラッシュを出す前にこのパスごと諦める
        for (const std::string& readName : step_.reads) {
            D3D12_GPU_DESCRIPTOR_HANDLE srv{};
            if (context.frameBlackboard) {
                context.frameBlackboard->TryGetSrvHandle(readName, srv);
            }
            if (srv.ptr == 0) {
#ifdef _DEBUG
                OutputDebugStringA("[PostEffectPass] WARNING: 入力リソースが未解決のためポストエフェクト実行を中断します。\n");
#endif
                return;
            }
            passContext.reads.push_back(srv);
        }

        if (!ResolveExtraInputs(context)) {
            return;
        }

        RenderTarget* outputTarget = ResolvePostEffectOutputTarget(context, step_.write);
        if (!outputTarget) {
            return;
        }

        auto* cmdList = context.cmdList;

        if (step_.executionType == PostEffectExecutionType::Compute) {
            auto* offscreen = dynamic_cast<OffscreenRenderTarget*>(outputTarget);
            if (!offscreen) {
                return;
            }

            passContext.output = offscreen->GetUAVHandle();
            passContext.width  = static_cast<uint32_t>(offscreen->GetWidth());
            passContext.height = static_cast<uint32_t>(offscreen->GetHeight());

            offscreen->BeginCS(cmdList);
            step_.record(passContext);
            offscreen->EndCS(cmdList);
        } else {
            passContext.width  = static_cast<uint32_t>(outputTarget->GetWidth());
            passContext.height = static_cast<uint32_t>(outputTarget->GetHeight());

            outputTarget->Begin(cmdList);
            step_.record(passContext);
            outputTarget->End(cmdList);
        }

        if (context.frameBlackboard) {
            context.frameBlackboard->SetResource(
                step_.write, outputTarget->GetSRVHandle(), &outputTarget->Resource());
        }
    }
}
