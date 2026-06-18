#pragma once
#include "RenderPass.h"
#include "Graphics/Render/RenderGraph.h"
#include <vector>
#include <memory>
#include <string>
#include <functional>

namespace CoreEngine
{
    /// @brief レンダリングパイプラインを管理するクラス
    class RenderPipeline {
    public:
        RenderPipeline() = default;
        ~RenderPipeline() = default;

        /// @brief レンダーパスを追加
        /// @param pass 追加するレンダーパス
        void AddPass(std::unique_ptr<RenderPass> pass);

        /// @brief 名前でレンダーパスを取得
        /// @param name パス名
        /// @return レンダーパスのポインタ（見つからない場合nullptr）
        RenderPass* GetPass(const std::string& name);

        /// @brief 型でレンダーパスを取得
        /// @tparam T レンダーパスの型
        /// @return レンダーパスのポインタ（見つからない場合nullptr）
        template<typename T>
        T* GetPass() {
            for (auto& pass : passes_) {
                if (auto* castedPass = dynamic_cast<T*>(pass.get())) {
                    return castedPass;
                }
            }
            return nullptr;
        }

        /// @brief パイプラインを実行
        /// @param context レンダリングコンテキスト
        void Execute(const RenderContext& context);

        /// @brief フレーム実行前のパス設定を行う
        /// @param context レンダリングコンテキスト
        /// @param geometryRenderCallback GeometryPass に設定する描画コールバック
        void PrepareFrame(const RenderContext& context, const std::function<void()>& geometryRenderCallback);

        /// @brief 最小 RenderGraph を構築する
        /// @param context レンダリングコンテキスト
        void BuildRenderGraph(const RenderContext& context);

        /// @brief 構築済み RenderGraph を実行する
        /// @param context レンダリングコンテキスト
        void ExecuteRenderGraph(const RenderContext& context);

        /// @brief 単一パスを実行し、前段出力を更新する
        /// @param pass 実行対象のパス
        /// @param context レンダリングコンテキスト
        void ExecutePass(RenderPass* pass, const RenderContext& context);

        /// @brief パスチェーンの前段出力状態をリセットする
        void ResetExecutionState();

        /// @brief 現在保持している前段出力を取得する
        /// @return 直前に実行されたパスの出力
        const PassOutput& GetPreviousOutput() const { return previousOutput_; }

        /// @brief 前段出力を外部から復元する
        /// @param output 復元する出力状態
        void SetPreviousOutput(const PassOutput& output) { previousOutput_ = output; }

        /// @brief すべてのパスをクリア
        void Clear();

        /// @brief パスの数を取得
        /// @return パスの数
        size_t GetPassCount() const { return passes_.size(); }

    private:
        /// @brief View 設定に応じて各パスの有効状態と出力先を切り替える
        /// @param context レンダリングコンテキスト
        void ConfigurePassesForView(const RenderContext& context);

        std::vector<std::unique_ptr<RenderPass>> passes_;
        RenderGraph renderGraph_{};
        PassOutput previousOutput_{};
    };
}
