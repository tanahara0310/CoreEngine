#pragma once
#include "RenderPass.h"
#include "Graphics/Render/RenderGraph.h"
#include <vector>
#include <memory>
#include <string>
#include <functional>

namespace CoreEngine
{
    struct ReflectionViewResult;

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

        /// @brief View 単位でフレーム準備から Graph 実行までを行う
        /// @param context レンダリングコンテキスト
        /// @param geometryRenderCallback GeometryPass に設定する描画コールバック
        /// @param beforeExecute Graph 実行直前に呼ぶコールバック
        /// @param afterExecute Graph 実行直後に呼ぶコールバック
        void ExecuteView(
            const RenderContext& context,
            const std::function<void()>& geometryRenderCallback,
            const std::function<void()>& beforeExecute = {},
            const std::function<void()>& afterExecute = {});

        /// @brief ReflectionView を実行し、シーン側へ返す共有結果を収集する
        /// @param context ReflectionView 用に構成済みのレンダリングコンテキスト
        /// @param geometryRenderCallback GeometryPass に設定する描画コールバック
        /// @param beforeExecute Graph 実行直前に呼ぶコールバック
        /// @param afterExecute Graph 実行直後に呼ぶコールバック
        /// @return ReflectionColor / SceneDepth / SceneColor をまとめた結果
        ReflectionViewResult ExecuteReflectionView(
            const RenderContext& context,
            const std::function<void()>& geometryRenderCallback,
            const std::function<void()>& beforeExecute = {},
            const std::function<void()>& afterExecute = {});

        /// @brief すべてのパスをクリア
        void Clear();

        /// @brief パスの数を取得
        /// @return パスの数
        size_t GetPassCount() const { return passes_.size(); }

    private:
        /// @brief RenderGraph 構築前に主要リソースを Blackboard へ登録する
        /// @param context レンダリングコンテキスト
        void RegisterFrameResources(const RenderContext& context);

        /// @brief View 設定に応じて各パスの有効状態と出力先を切り替える
        /// @param context レンダリングコンテキスト
        void ConfigurePassesForView(const RenderContext& context);

        /// @brief ReflectionView 実行後の共有結果を収集する
        /// @param context ReflectionView 実行に使用したレンダリングコンテキスト
        /// @return ReflectionView の共有結果
        ReflectionViewResult BuildReflectionViewResult(const RenderContext& context) const;

        std::vector<std::unique_ptr<RenderPass>> passes_;
        RenderGraph renderGraph_{};
    };
}
