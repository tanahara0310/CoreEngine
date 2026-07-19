#include "pch.h"
#include "RenderGraph.h"

#include <algorithm>
#include <cassert>
#include <queue>

#include "Graphics/Common/DirectXCommon.h"
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
        const std::function<void(RenderGraphBuilder&)>& setup,
        GpuTimingCategory timingCategory)
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
        const uint32_t passIndex = static_cast<uint32_t>(passes_.size());

        RenderGraphPass pass;
        pass.name = name;
        pass.renderPass = renderPass;
        pass.reads = builder.GetReads();
        pass.writes = builder.GetWrites();
        pass.timingCategory = timingCategory;

        std::vector<uint32_t> dependencies;

        // Read (RAW): 現行バージョンのライターへ依存し、自身をそのバージョンの読者として登録する。
        for (const RenderGraphResourceAccess& readAccess : pass.reads) {
            RenderGraphResource& resource = resources_[readAccess.resourceName];
            resource.name = readAccess.resourceName;
            if (resource.hasWriter) {
                dependencies.push_back(resource.lastWriterIndex);
            }
            if (std::find(resource.readers.begin(), resource.readers.end(), passIndex)
                == resource.readers.end()) {
                resource.readers.push_back(passIndex);
            }
        }

        // Write (WAW + WAR): 現行ライターと現行バージョンの全読者へ依存したうえで、
        // 新バージョンのライターとなり読者リストをリセットする。
        for (const RenderGraphResourceAccess& writeAccess : pass.writes) {
            RenderGraphResource& resource = resources_[writeAccess.resourceName];
            resource.name = writeAccess.resourceName;
            if (resource.hasWriter) {
                dependencies.push_back(resource.lastWriterIndex);
            }
            for (uint32_t reader : resource.readers) {
                dependencies.push_back(reader);
            }
            resource.lastWriterIndex = passIndex;
            resource.hasWriter = true;
            resource.readers.clear();
            ++resource.version;
        }

        // 自己依存を除去し、重複をまとめる。
        dependencies.erase(
            std::remove(dependencies.begin(), dependencies.end(), passIndex),
            dependencies.end());
        std::sort(dependencies.begin(), dependencies.end());
        dependencies.erase(
            std::unique(dependencies.begin(), dependencies.end()),
            dependencies.end());
        pass.dependencies = std::move(dependencies);

        passes_.push_back(std::move(pass));
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

        // 実行可能ノードのうち最小 index を常に選ぶことで、
        // 依存を満たす範囲で登録順と一致する決定的な実行順を得る。
        std::priority_queue<uint32_t, std::vector<uint32_t>, std::greater<uint32_t>> ready;
        for (uint32_t passIndex = 0; passIndex < indegree.size(); ++passIndex) {
            if (indegree[passIndex] == 0) {
                ready.push(passIndex);
            }
        }

        while (!ready.empty()) {
            const uint32_t current = ready.top();
            ready.pop();
            executionOrder_.push_back(current);

            for (uint32_t next : edges[current]) {
                if (--indegree[next] == 0) {
                    ready.push(next);
                }
            }
        }

        if (executionOrder_.size() != passes_.size()) {
            // 依存は常に先行パスのみを参照する構築規則のため、ここへの到達は Graph 実装のバグ。
            Logger::GetInstance().Logf(
                LogLevel::Error,
                LogCategory::Graphics,
                LogSubCategory::Barrier,
                "[RenderGraph] Cycle detected: resolved {} of {} passes. Falling back to registration order.",
                executionOrder_.size(),
                passes_.size());
            assert(false && "RenderGraph::Compile detected a dependency cycle");

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

            RenderGraphPass& graphPass = passes_[passIndex];
            RenderPass* renderPass = graphPass.renderPass;
            if (!renderPass || !renderPass->IsEnabled()) {
                continue;
            }

            // パス名から動的タイミングスロットを解決し、Setup～Cleanup 全体を計測する。
            // これにより新規パスは登録するだけで自動的にタイミング表示へ現れる。
            GpuTimestampProfiler* profiler = context.renderContext->gpuProfiler;
            ID3D12GraphicsCommandList* profileCmdList =
                (profiler && context.renderContext->dxCommon) ? context.renderContext->dxCommon->GetCommandList() : nullptr;
            uint32_t timingSlot = UINT32_MAX;
            if (profiler && profileCmdList) {
                timingSlot = profiler->GetOrCreateNamedSlot(graphPass.name, graphPass.timingCategory);
                if (timingSlot != UINT32_MAX) {
                    profiler->BeginCpuTimestamp(timingSlot);
                    profiler->BeginGpuTimestamp(timingSlot, profileCmdList);
                }
            }

            ApplyTransitionsForPass(graphPass, *context.renderContext);
            renderPass->Setup(*context.renderContext);
            renderPass->Execute(*context.renderContext);
            renderPass->Cleanup(*context.renderContext);

            if (timingSlot != UINT32_MAX) {
                profiler->EndGpuTimestamp(timingSlot, profileCmdList);
                profiler->EndCpuTimestamp(timingSlot);
            }
        }
    }

    void RenderGraph::ResolveResources(const RenderContext& context)
    {
        // Blackboard を唯一の解決元とする。ここで未解決のリソースは、
        // ApplyTransitionsForPass の実行時再解決に委ねる。
        // （旧 RTShadowMask の View 特例は RenderPipeline::RegisterFrameResources へ移設済み）
        for (auto& [resourceName, resource] : resources_) {
            resource.resource = nullptr;
            resource.currentState = nullptr;

            if (context.frameBlackboard) {
                context.frameBlackboard->TryResolveResource(resourceName, resource.resource, resource.currentState);
            }
        }
    }

    void RenderGraph::ApplyTransitionsForPass(const RenderGraphPass& pass, const RenderContext& context)
    {
        // 各ノードの要求状態に合わせて、実行前に自動バリアをバッチ発行する。
        if (!context.dxCommon) {
            return;
        }

        ID3D12GraphicsCommandList* cmdList = context.dxCommon->GetCommandList();
        if (!cmdList) {
            return;
        }

        // 同一パス内で同じリソースに Read と Write の両方が宣言された場合、
        // 中間状態を経由せず最終的に必要な状態（Write 優先）へ 1 回で遷移する。
        struct MergedAccess {
            const std::string* resourceName;
            D3D12_RESOURCE_STATES requiredState;
            bool isWrite;
        };
        std::vector<MergedAccess> mergedAccesses;
        mergedAccesses.reserve(pass.reads.size() + pass.writes.size());

        auto mergeAccess = [&mergedAccesses](const RenderGraphResourceAccess& access, bool isWrite) {
            for (MergedAccess& entry : mergedAccesses) {
                if (*entry.resourceName == access.resourceName) {
                    if (isWrite) {
                        entry.requiredState = access.requiredState;
                        entry.isWrite = true;
                    }
                    return;
                }
            }
            mergedAccesses.push_back({ &access.resourceName, access.requiredState, isWrite });
        };

        for (const RenderGraphResourceAccess& readAccess : pass.reads) {
            mergeAccess(readAccess, false);
        }
        for (const RenderGraphResourceAccess& writeAccess : pass.writes) {
            mergeAccess(writeAccess, true);
        }

        ResourceBarrierBatch barrierBatch(cmdList);

        for (const MergedAccess& access : mergedAccesses) {
            auto it = resources_.find(*access.resourceName);
            if (it == resources_.end()) {
                continue;
            }
            RenderGraphResource& resource = it->second;

            // Compile 時に未解決だったリソースは、先行パスが実行中に Blackboard へ
            // 登録した可能性があるため、実行直前に再解決を試みる（SSAO / RT 系の遅延登録対応）。
            if ((!resource.resource || !resource.currentState) && context.frameBlackboard) {
                context.frameBlackboard->TryResolveResource(
                    *access.resourceName, resource.resource, resource.currentState);
            }

            if (!resource.resource || !resource.currentState) {
#ifdef _DEBUG
                Logger::GetInstance().Logf(
                    LogLevel::Debug,
                    LogCategory::Graphics,
                    LogSubCategory::Barrier,
                    "[RenderGraph] View={} Pass={} Unresolved={} (barrier skipped)",
                    RenderViewTypeToString(context.viewSettings.viewType),
                    pass.name,
                    *access.resourceName);
#endif
                continue;
            }

#ifdef _DEBUG
            Logger::GetInstance().Logf(
                LogLevel::Debug,
                LogCategory::Graphics,
                LogSubCategory::Barrier,
                "[RenderGraph] View={} Pass={} {}={} resource=0x{:X} current=0x{:X} required=0x{:X}",
                RenderViewTypeToString(context.viewSettings.viewType),
                pass.name,
                access.isWrite ? "Write" : "Read",
                *access.resourceName,
                reinterpret_cast<uintptr_t>(resource.resource),
                static_cast<uint32_t>(*resource.currentState),
                static_cast<uint32_t>(access.requiredState));
#endif

            // UNORDERED_ACCESS のまま連続書き込みする場合は遷移が発生しないため、
            // UAV バリアで書き込みの直列化を保証する。
            if (access.isWrite
                && access.requiredState == D3D12_RESOURCE_STATE_UNORDERED_ACCESS
                && *resource.currentState == D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
                barrierBatch.AddUAV(resource.resource);
            } else {
                barrierBatch.Add(resource.resource, *resource.currentState, access.requiredState);
            }
        }

        barrierBatch.Flush();
    }
}
