#include "pch.h"
#include "RenderGraph.h"

#include <algorithm>
#include <queue>

#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Common/Core/DepthStencilManager.h"
#include "Graphics/RayTracing/RayTracingShadowManager.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Graphics/Render/RenderTarget/RenderTargetNames.h"
#include "Graphics/Shadow/ShadowMapManager.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    namespace {
#ifdef _DEBUG
        const char* RenderViewTypeToString(RenderViewType viewType)
        {
            switch (viewType) {
            case RenderViewType::GameView:
                return "GameView";
            case RenderViewType::ReflectionView:
                return "ReflectionView";
            case RenderViewType::CaptureView:
                return "CaptureView";
            default:
                return "UnknownView";
            }
        }
#endif
    }

    void RenderGraphBuilder::Read(
        const std::string& resourceName,
        D3D12_RESOURCE_STATES requiredState)
    {
        // 読み取りリソースと必要状態を Graph へ記録する。
        reads_.push_back({ resourceName, requiredState });
    }

    void RenderGraphBuilder::Write(
        const std::string& resourceName,
        D3D12_RESOURCE_STATES requiredState)
    {
        // 書き込みリソースと必要状態を Graph へ記録する。
        writes_.push_back({ resourceName, requiredState });
    }

    void RenderGraph::Reset()
    {
        // フレームごとの Graph ノードと状態追跡情報を初期化する。
        passes_.clear();
        resources_.clear();
        executionOrder_.clear();
    }

    void RenderGraph::AddPass(
        const std::string& name,
        RenderPass* renderPass,
        const std::function<void(RenderGraphBuilder&)>& setup)
    {
        if (!renderPass) {
            return;
        }

        // 各パスの Read / Write 宣言をいったんビルダーへ蓄積する。
        RenderGraphBuilder builder;
        if (setup) {
            setup(builder);
        }

        // Graph ノードへ宣言済みリソースアクセスを転記する。
        RenderGraphPass pass;
        pass.name = name;
        pass.renderPass = renderPass;
        pass.reads = builder.GetReads();
        pass.writes = builder.GetWrites();

        RegisterDependencies(pass);

        const uint32_t passIndex = static_cast<uint32_t>(passes_.size());
        passes_.push_back(pass);

        // 書き込み先ごとに最新ライターを更新し、後続依存計算へ使う。
        for (const RenderGraphResourceAccess& writeAccess : builder.GetWrites()) {
            RenderGraphResource& resource = resources_[writeAccess.resourceName];
            resource.name = writeAccess.resourceName;
            resource.lastWriterIndex = passIndex;
            resource.hasWriter = true;
        }
    }

    void RenderGraph::Compile(const RenderContext& context)
    {
        // Graph 実行前に主要リソースの実体と状態参照を補完する。
        ResolveResources(context);

        // 依存関係から実行順を再計算する。
        executionOrder_.clear();
        if (passes_.empty()) {
            return;
        }

        std::vector<uint32_t> indegree(passes_.size(), 0);
        std::vector<std::vector<uint32_t>> edges(passes_.size());

        for (uint32_t passIndex = 0; passIndex < passes_.size(); ++passIndex) {
            for (uint32_t dependency : passes_[passIndex].dependencies) {
                if (dependency >= passes_.size() || dependency == passIndex) {
                    continue;
                }
                edges[dependency].push_back(passIndex);
                ++indegree[passIndex];
            }
        }

        std::queue<uint32_t> ready;
        for (uint32_t passIndex = 0; passIndex < indegree.size(); ++passIndex) {
            if (indegree[passIndex] == 0) {
                ready.push(passIndex);
            }
        }

        while (!ready.empty()) {
            const uint32_t current = ready.front();
            ready.pop();
            executionOrder_.push_back(current);

            for (uint32_t next : edges[current]) {
                if (--indegree[next] == 0) {
                    ready.push(next);
                }
            }
        }

        if (executionOrder_.size() != passes_.size()) {
            // 循環依存などで解決できない場合は登録順へフォールバックする。
            executionOrder_.clear();
            for (uint32_t passIndex = 0; passIndex < passes_.size(); ++passIndex) {
                executionOrder_.push_back(passIndex);
            }
        }
    }

    void RenderGraph::Execute(const RenderGraphContext& context)
    {
        if (!context.renderContext) {
            return;
        }

        // 実行順に従って、必要バリアを張ってから各パスを実行する。
        for (uint32_t passIndex : executionOrder_) {
            if (passIndex >= passes_.size()) {
                continue;
            }

            RenderPass* renderPass = passes_[passIndex].renderPass;
            if (!renderPass || !renderPass->IsEnabled()) {
                continue;
            }

            ApplyTransitionsForPass(passes_[passIndex], *context.renderContext);
            renderPass->Setup(*context.renderContext);
            renderPass->Execute(*context.renderContext);
            renderPass->Cleanup(*context.renderContext);
        }
    }

    void RenderGraph::ResolveResources(const RenderContext& context)
    {
        // Blackboard を正本として優先し、未登録分だけ最小限に補完する。
        for (auto& [resourceName, resource] : resources_) {
            resource.resource = nullptr;
            resource.currentState = nullptr;

            if (context.frameBlackboard &&
                context.frameBlackboard->TryResolveResource(resourceName, resource.resource, resource.currentState)) {
                continue;
            }

            // RTShadowMask は View 依存で実行後に更新されるため、この段階では最小限の fallback を残す。
            if (resourceName == FrameBlackboard::RTShadowMask && context.rtShadowManager) {
                const RayTracingShadowManager::ViewID viewId =
                    (context.currentRTShadowViewId == static_cast<uint32_t>(RayTracingShadowManager::ViewID::ReflectionView))
                    ? RayTracingShadowManager::ViewID::ReflectionView
                    : RayTracingShadowManager::ViewID::GameView;
                resource.resource = context.rtShadowManager->GetShadowResource(viewId, 0);
                resource.currentState = &context.rtShadowManager->GetShadowCurrentState(viewId, 0);
                continue;
            }
        }
    }

    void RenderGraph::ApplyTransitionsForPass(const RenderGraphPass& pass, const RenderContext& context) const
    {
        // 各ノードの要求状態に合わせて、実行前に自動バリアを発行する。
        if (!context.dxCommon) {
            return;
        }

        ID3D12GraphicsCommandList* cmdList = context.dxCommon->GetCommandList();
        if (!cmdList) {
            return;
        }

        for (const RenderGraphResourceAccess& readAccess : pass.reads) {
            auto it = resources_.find(readAccess.resourceName);
            if (it == resources_.end() || !it->second.resource || !it->second.currentState) {
                continue;
            }

#ifdef _DEBUG
            Logger::GetInstance().Logf(
                LogLevel::Debug,
                LogCategory::Graphics,
                LogSubCategory::Barrier,
                "[RenderGraph] View={} Pass={} Read={} resource=0x{:X} current=0x{:X} required=0x{:X}",
                RenderViewTypeToString(context.viewSettings.viewType),
                pass.name,
                readAccess.resourceName,
                reinterpret_cast<uintptr_t>(it->second.resource),
                static_cast<uint32_t>(*it->second.currentState),
                static_cast<uint32_t>(readAccess.requiredState));
#endif

            ResourceBarrierHelper::Transition(
                cmdList,
                it->second.resource,
                *it->second.currentState,
                readAccess.requiredState);
        }

        for (const RenderGraphResourceAccess& writeAccess : pass.writes) {
            auto it = resources_.find(writeAccess.resourceName);
            if (it == resources_.end() || !it->second.resource || !it->second.currentState) {
                continue;
            }

#ifdef _DEBUG
            Logger::GetInstance().Logf(
                LogLevel::Debug,
                LogCategory::Graphics,
                LogSubCategory::Barrier,
                "[RenderGraph] View={} Pass={} Write={} resource=0x{:X} current=0x{:X} required=0x{:X}",
                RenderViewTypeToString(context.viewSettings.viewType),
                pass.name,
                writeAccess.resourceName,
                reinterpret_cast<uintptr_t>(it->second.resource),
                static_cast<uint32_t>(*it->second.currentState),
                static_cast<uint32_t>(writeAccess.requiredState));
#endif

            ResourceBarrierHelper::Transition(
                cmdList,
                it->second.resource,
                *it->second.currentState,
                writeAccess.requiredState);
        }
    }

    void RenderGraph::RegisterDependencies(RenderGraphPass& pass)
    {
        std::vector<uint32_t> dependencies;

        // 読み取り元・書き込み先の直前ライターを依存として束ねる。
        for (const RenderGraphResourceAccess& readAccess : pass.reads) {
            auto it = resources_.find(readAccess.resourceName);
            if (it != resources_.end() && it->second.hasWriter) {
                dependencies.push_back(it->second.lastWriterIndex);
            }
        }

        for (const RenderGraphResourceAccess& writeAccess : pass.writes) {
            auto it = resources_.find(writeAccess.resourceName);
            if (it != resources_.end() && it->second.hasWriter) {
                dependencies.push_back(it->second.lastWriterIndex);
            }
        }

        std::sort(dependencies.begin(), dependencies.end());
        dependencies.erase(std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
        pass.dependencies = std::move(dependencies);
    }
}
