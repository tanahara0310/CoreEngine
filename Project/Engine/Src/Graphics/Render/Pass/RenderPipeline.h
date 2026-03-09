#pragma once
#include "RenderPass.h"
#include <vector>
#include <memory>
#include <string>

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

        /// @brief すべてのパスをクリア
        void Clear();

        /// @brief パスの数を取得
        /// @return パスの数
        size_t GetPassCount() const { return passes_.size(); }

    private:
        std::vector<std::unique_ptr<RenderPass>> passes_;
    };
}
