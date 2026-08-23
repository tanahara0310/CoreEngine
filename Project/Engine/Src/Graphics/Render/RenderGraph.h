#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Graphics/RHI/Debug/GpuTimestampProfiler.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "Pass/RenderPass.h"

namespace CoreEngine
{
    /// @brief グラフが追跡する論理リソース 1 つ分の状態（版番号・直近の書き手・読み手）
    struct RenderGraphResource {
        std::string name;
        uint32_t lastWriterIndex = 0;
        bool hasWriter = false;
        uint32_t version = 0;              ///< Write 宣言ごとにインクリメントされる版番号
        std::vector<uint32_t> readers;     ///< 現行バージョンを読むパス一覧（WAR 依存の計算元）
        GpuResource* resource = nullptr;  ///< 実リソースと現在ステート（Blackboard から解決される）
    };

    /// @brief 1 リソースへのアクセス宣言（論理名と必要な D3D12 ステート）
    struct RenderGraphResourceAccess {
        std::string resourceName;
        D3D12_RESOURCE_STATES requiredState = D3D12_RESOURCE_STATE_COMMON;
    };

    /// @brief パス間依存が生まれた理由
    /// @details 実行順は「宣言から導出される」ため、順番の根拠はコードのどこにも書かれていない。
    ///          原因を保持しておくと「なぜこのパスがこの位置なのか」を後から説明できる。
    enum class RenderGraphDependencyKind : uint8_t {
        ReadAfterWrite = 0, ///< RAW: 先行パスの書き込み結果を読む
        WriteAfterWrite,    ///< WAW: 同じリソースへ上書きする
        WriteAfterRead,     ///< WAR: 先行パスが読み終わる前に上書きしない
    };

    /// @brief 依存エッジ 1 本（依存先パスと、その依存を生んだ論理リソース）
    struct RenderGraphDependency {
        uint32_t passIndex = 0;
        std::string resourceName;
        RenderGraphDependencyKind kind = RenderGraphDependencyKind::ReadAfterWrite;

        bool operator==(const RenderGraphDependency& other) const {
            return passIndex == other.passIndex
                && kind == other.kind
                && resourceName == other.resourceName;
        }
    };

    /// @brief 実行時に発行したバリア 1 件の記録（計装が有効なときのみ積まれる）
    struct RenderGraphBarrierRecord {
        std::string resourceName;
        D3D12_RESOURCE_STATES beforeState = D3D12_RESOURCE_STATE_COMMON;
        D3D12_RESOURCE_STATES afterState = D3D12_RESOURCE_STATE_COMMON;
        bool isUavBarrier = false;
        bool isWrite = false;
    };

    /// @brief 依存の種別を表示用文字列へ変換する
    /// @param kind 依存種別
    /// @return "RAW" / "WAW" / "WAR"
    constexpr const char* ToString(RenderGraphDependencyKind kind) noexcept
    {
        switch (kind) {
        case RenderGraphDependencyKind::ReadAfterWrite:  return "RAW";
        case RenderGraphDependencyKind::WriteAfterWrite: return "WAW";
        case RenderGraphDependencyKind::WriteAfterRead:  return "WAR";
        default:                                         return "?";
        }
    }

    /// @brief パスが自分の読み書きを宣言するための入口
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

        /// @brief UAV 書き込みリソースを登録する
        /// @details UNORDERED_ACCESS 状態のまま連続書き込みされる場合、
        ///          Graph が遷移バリアの代わりに UAV バリアを自動発行する。
        /// @param resourceName 論理リソース名
        void WriteUAV(const std::string& resourceName) {
            Write(resourceName, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }

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

    /// @brief グラフのノード 1 つ（パスとその読み書き宣言・導出された依存）
    struct RenderGraphPass {
        std::string name;
        RenderPass* renderPass = nullptr;
        std::vector<RenderGraphResourceAccess> reads;
        std::vector<RenderGraphResourceAccess> writes;
        std::vector<RenderGraphDependency> dependencies;
        GpuTimingCategory timingCategory = GpuTimingCategory::Setup; ///< タイミング表示のグルーピングカテゴリ

        // ── 以下は Execute 中に埋まる計装データ（RenderGraph::SetInstrumentationEnabled が真のときのみ）──
        bool executed = false;                                 ///< 今フレームこの View で実際に実行されたか
        std::vector<RenderGraphBarrierRecord> barriers;        ///< 実行直前に発行したバリア
        std::vector<std::string> unresolvedResources;          ///< 実体を解決できずバリアを飛ばしたリソース
    };

    /// @brief グラフ実行時にパスへ渡す文脈
    struct RenderGraphContext {
        const RenderContext* renderContext = nullptr;
    };

    /// @brief 宣言された読み書きから実行順とバリアを導出し、パスを実行するグラフ
    class RenderGraph {
    public:
        /// @brief グラフ内の全ノードと状態追跡情報をクリアする
        void Reset();

        /// @brief パスを Graph へ登録する
        /// @param name パス名
        /// @param renderPass 実行対象パス
        /// @param setup Read / Write 宣言を行うビルダー設定関数
        /// @param timingCategory タイミング表示のグルーピングカテゴリ
        void AddPass(
            const std::string& name,
            RenderPass* renderPass,
            const std::function<void(RenderGraphBuilder&)>& setup,
            GpuTimingCategory timingCategory = GpuTimingCategory::Setup);

        /// @brief Graph 内の依存関係を解決して実行順を確定する
        /// @brief Blackboard / Manager から実リソースを解決したうえで依存関係を確定する
        /// @param context 主要リソースの実体を補完する描画コンテキスト
        void Compile(const RenderContext& context);

        /// @brief 確定済みの Graph を実行する
        /// @param context RenderContext を束ねた実行情報
        void Execute(const RenderGraphContext& context);

        const std::vector<RenderGraphPass>& GetPasses() const { return passes_; }
        const std::vector<uint32_t>& GetExecutionOrder() const { return executionOrder_; }
        const std::unordered_map<std::string, RenderGraphResource>& GetResources() const { return resources_; }

        /// @brief 計装（実行フラグ・バリア記録・未解決リソース記録）の有効/無効を切り替える
        /// @details エディタが Graph を覗いていないフレームでは記録コストをゼロにするためのスイッチ。
        ///          無効時は Execute 中の記録用 vector へ一切触れない。
        /// @param enabled 記録する場合 true
        void SetInstrumentationEnabled(bool enabled) { instrumentationEnabled_ = enabled; }

        /// @brief 計装が有効かを返す
        bool IsInstrumentationEnabled() const { return instrumentationEnabled_; }

    private:
        /// @brief Blackboard と既存 Manager から主要リソースの実体と状態参照を解決する
        /// @param context 実行時コンテキスト
        void ResolveResources(const RenderContext& context);

        /// @brief 1 パス実行前に必要な状態遷移をバッチで自動発行する
        /// @details 同一リソースへの Read + Write 宣言は最終状態（Write 優先）へ 1 回で遷移する。
        ///          Compile 時に未解決だったリソースは実行時に Blackboard から再解決を試みる。
        /// @param pass 実行対象 Graph パス
        /// @param context 実行時コンテキスト
        void ApplyTransitionsForPass(RenderGraphPass& pass, const RenderContext& context);

        std::vector<RenderGraphPass> passes_;
        std::unordered_map<std::string, RenderGraphResource> resources_;
        std::vector<uint32_t> executionOrder_;
        bool instrumentationEnabled_ = false;
    };
}
