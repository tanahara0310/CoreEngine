#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Graphics/Common/ResourceBarrierHelper.h"
#include "Pass/RenderPass.h"

namespace CoreEngine
{
    struct RenderGraphResource {
        std::string name;
        uint32_t lastWriterIndex = 0;
        bool hasWriter = false;
        ID3D12Resource* resource = nullptr;
        D3D12_RESOURCE_STATES* currentState = nullptr;
    };

    struct RenderGraphResourceAccess {
        std::string resourceName;
        D3D12_RESOURCE_STATES requiredState = D3D12_RESOURCE_STATE_COMMON;
    };

    class RenderGraphBuilder {
    public:
        /// @brief 読み取りリソースを登録する
        /// @param resourceName 論理リソース名
        /// @param requiredState 読み取りに必要な D3D12 リソース状態
        void Read(
            const std::string& resourceName,
            D3D12_RESOURCE_STATES requiredState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        /// @brief 書き込みリソースを登録する
        /// @param resourceName 論理リソース名
        /// @param requiredState 書き込みに必要な D3D12 リソース状態
        void Write(
            const std::string& resourceName,
            D3D12_RESOURCE_STATES requiredState = D3D12_RESOURCE_STATE_RENDER_TARGET);

        /// @brief 登録済み読み取り情報を取得する
        /// @return 読み取りリソース一覧
        const std::vector<RenderGraphResourceAccess>& GetReads() const { return reads_; }

        /// @brief 登録済み書き込み情報を取得する
        /// @return 書き込みリソース一覧
        const std::vector<RenderGraphResourceAccess>& GetWrites() const { return writes_; }

    private:
        std::vector<RenderGraphResourceAccess> reads_;
        std::vector<RenderGraphResourceAccess> writes_;
    };

    struct RenderGraphPass {
        std::string name;
        RenderPass* renderPass = nullptr;
        std::vector<RenderGraphResourceAccess> reads;
        std::vector<RenderGraphResourceAccess> writes;
        std::vector<uint32_t> dependencies;
    };

    struct RenderGraphContext {
        const RenderContext* renderContext = nullptr;
    };

    class RenderGraph {
    public:
        /// @brief グラフ内の全ノードと状態追跡情報をクリアする
        void Reset();

        /// @brief パスを Graph へ登録する
        /// @param name パス名
        /// @param renderPass 実行対象パス
        /// @param setup Read / Write 宣言を行うビルダー設定関数
        void AddPass(
            const std::string& name,
            RenderPass* renderPass,
            const std::function<void(RenderGraphBuilder&)>& setup);

        /// @brief Graph 内の依存関係を解決して実行順を確定する
        /// @brief Blackboard / Manager から実リソースを解決したうえで依存関係を確定する
        /// @param context 主要リソースの実体を補完する描画コンテキスト
        void Compile(const RenderContext& context);

        /// @brief 確定済みの Graph を実行する
        /// @param context RenderContext を束ねた実行情報
        void Execute(const RenderGraphContext& context);

        const std::vector<RenderGraphPass>& GetPasses() const { return passes_; }
        const std::vector<uint32_t>& GetExecutionOrder() const { return executionOrder_; }

    private:
        /// @brief Blackboard と既存 Manager から主要リソースの実体と状態参照を解決する
        /// @param context 実行時コンテキスト
        void ResolveResources(const RenderContext& context);

        /// @brief 1 パス実行前に必要な状態遷移を自動発行する
        /// @param pass 実行対象 Graph パス
        /// @param context 実行時コンテキスト
        void ApplyTransitionsForPass(const RenderGraphPass& pass, const RenderContext& context) const;

        /// @brief パス依存情報を登録する
        /// @param pass 依存を計算する Graph パス
        void RegisterDependencies(RenderGraphPass& pass);

        std::vector<RenderGraphPass> passes_;
        std::unordered_map<std::string, RenderGraphResource> resources_;
        std::vector<uint32_t> executionOrder_;
    };
}
