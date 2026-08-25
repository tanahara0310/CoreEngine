#include "pch.h"
#include "PostEffectGraphBuilder.h"
#include "PostEffectTransientPool.h"

#include "Graphics/Render/Pass/RenderPass.h"
#include "Graphics/Render/FrameBlackboard.h"
#include "Graphics/Render/RenderTarget/OffscreenRenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>

namespace CoreEngine
{
    namespace {
        /// 一時ターゲットの既定フォーマット。SceneHDR 段を通るので HDR で持つ
        constexpr DXGI_FORMAT kDefaultTransientFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    }

    PostEffectGraphBuilder::PostEffectGraphBuilder(
        const RenderContext& context,
        PostEffectTransientPool& pool,
        std::string inputResource,
        std::string chainOutput,
        uint32_t baseWidth,
        uint32_t baseHeight)
        : context_(context)
        , pool_(pool)
        , inputResource_(std::move(inputResource))
        , chainOutput_(std::move(chainOutput))
        , baseWidth_(baseWidth)
        , baseHeight_(baseHeight)
    {
    }

    PostEffectResourceRef PostEffectGraphBuilder::CreateTransient(float resolutionScale, DXGI_FORMAT format)
    {
        if (!context_.renderTargetManager) {
            return {};
        }

        // 0 になると生成に失敗するので最低 1px は確保する
        const uint32_t width  = std::max(1u, static_cast<uint32_t>(static_cast<float>(baseWidth_)  * resolutionScale));
        const uint32_t height = std::max(1u, static_cast<uint32_t>(static_cast<float>(baseHeight_) * resolutionScale));
        const DXGI_FORMAT actualFormat = (format == DXGI_FORMAT_UNKNOWN) ? kDefaultTransientFormat : format;

        const std::string name = pool_.Acquire(context_.renderTargetManager, width, height, actualFormat);
        if (name.empty()) {
            Logger::GetInstance().Errorf(LogCategory::Graphics,
                "[PostEffect] 一時ターゲットの確保に失敗しました（{}x{}）", width, height);
            return {};
        }

        // Compile 時点で実体を解決できるよう、確保した直後に Blackboard へ載せる。
        // ここで登録しておかないとバリアの導出が実行時の再解決頼みになる。
        if (context_.frameBlackboard) {
            if (RenderTarget* target = context_.renderTargetManager->GetRenderTarget(name)) {
                context_.frameBlackboard->SetResource(
                    name, target->GetSRVHandle(), &target->Resource());
            }
        }

        return PostEffectResourceRef(name);
    }

    void PostEffectGraphBuilder::AddComputePass(
        std::string_view name,
        const std::vector<PostEffectResourceRef>& reads,
        const PostEffectResourceRef& write,
        PostEffectRecordFn record)
    {
        AddPass(name, reads, write, PostEffectExecutionType::Compute, std::move(record));
    }

    void PostEffectGraphBuilder::AddGraphicsPass(
        std::string_view name,
        const std::vector<PostEffectResourceRef>& reads,
        const PostEffectResourceRef& write,
        PostEffectRecordFn record)
    {
        AddPass(name, reads, write, PostEffectExecutionType::Graphics, std::move(record));
    }

    void PostEffectGraphBuilder::AddPass(
        std::string_view name,
        const std::vector<PostEffectResourceRef>& reads,
        const PostEffectResourceRef& write,
        PostEffectExecutionType executionType,
        PostEffectRecordFn record)
    {
        if (!write.IsValid() || !record) {
            Logger::GetInstance().Errorf(LogCategory::Graphics,
                "[PostEffect] パス '{}' は出力または記録処理が未設定です", std::string(name));
            return;
        }

        PostEffectStep step;
        step.name = std::string(name);
        step.reads.reserve(reads.size());
        for (const PostEffectResourceRef& ref : reads) {
            if (ref.IsValid()) {
                step.reads.push_back(ref.Name());
            }
        }
        step.write         = write.Name();
        step.executionType = executionType;
        step.record        = std::move(record);

        steps_.push_back(std::move(step));
    }
}
