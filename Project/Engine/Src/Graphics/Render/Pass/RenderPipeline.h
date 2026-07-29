#pragma once
#include "RenderPass.h"
#include "PostEffectPass.h"
#include "Graphics/Render/RenderGraph.h"
#include <cstdint>
#include <vector>
#include <memory>
#include <string>
#include <functional>

namespace CoreEngine
{
    struct RenderViewResult;

    /// @brief レンダリングパイプラインを管理するクラス
    class RenderPipeline {
    public:
        RenderPipeline() = default;
        ~RenderPipeline() = default;

        /// @brief レンダーパスを指定フェーズへ追加
        /// @details 同一フェーズ内は priority（小さいほど先）、同 priority は登録順。
        ///          エンジンコードを編集せずにユーザーパスを挿入する唯一の入口。
        /// @param pass 追加するレンダーパス
        /// @param phase 挿入フェーズ
        /// @param priority フェーズ内優先度
        /// @return 追加したパスへのポインタ（RemovePass 用ハンドル）
        RenderPass* AddPass(
            std::unique_ptr<RenderPass> pass,
            RenderPassPhase phase,
            int priority = 0);

        /// @brief レンダーパスを削除
        /// @param pass AddPass が返したパスポインタ
        void RemovePass(RenderPass* pass);

        /// @brief 所有者タグが一致する全パスを削除（シーン破棄時のユーザーパス一括除去）
        /// @param owner BeginOwnerScope で設定した所有者タグ
        void RemovePassesByOwner(const void* owner);

        /// @brief 以降の AddPass に所有者タグを付与する（SceneManager がシーン登録前後に呼ぶ）
        /// @param owner 所有者タグ（通常はシーンのポインタ）
        void BeginOwnerScope(const void* owner) { activeOwner_ = owner; }

        /// @brief 所有者タグの付与を終了する
        void EndOwnerScope() { activeOwner_ = nullptr; }

        /// @brief 名前でレンダーパスを取得
        /// @param name パス名
        /// @return レンダーパスのポインタ（見つからない場合nullptr）
        RenderPass* GetPass(const std::string& name);

        /// @brief 型でレンダーパスを取得
        /// @tparam T レンダーパスの型
        /// @return レンダーパスのポインタ（見つからない場合nullptr）
        template<typename T>
        T* GetPass() {
            for (auto& entry : passes_) {
                if (auto* castedPass = dynamic_cast<T*>(entry.pass.get())) {
                    return castedPass;
                }
            }
            return nullptr;
        }

        /// @brief フレーム実行前のパス設定を行う
        /// @param context レンダリングコンテキスト
        void PrepareFrame(const RenderContext& context);

        /// @brief 最小 RenderGraph を構築する
        /// @param context レンダリングコンテキスト
        void BuildRenderGraph(const RenderContext& context);

        /// @brief 構築済み RenderGraph を実行する
        /// @param context レンダリングコンテキスト
        void ExecuteRenderGraph(const RenderContext& context);

        /// @brief View 単位でフレーム準備から Graph 実行までを行う
        /// @param context レンダリングコンテキスト
        /// @param beforeExecute Graph 実行直前に呼ぶコールバック
        /// @param afterExecute Graph 実行直後に呼ぶコールバック
        void ExecuteView(
            const RenderContext& context,
            const std::function<void()>& beforeExecute = {},
            const std::function<void()>& afterExecute = {});

        /// @brief 補助 RenderView を実行し、呼び出し側へ共有結果を収集する
        /// @param context 補助 View 用に構成済みのレンダリングコンテキスト
        /// @param beforeExecute Graph 実行直前に呼ぶコールバック
        /// @param afterExecute Graph 実行直後に呼ぶコールバック
        /// @return View 出力 / SceneDepth / SceneColor をまとめた結果
        RenderViewResult ExecuteRenderView(
            const RenderContext& context,
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

        /// @brief PostEffect の有効エフェクト列を Graph ノードへ分解して追加する
        /// @param context レンダリングコンテキスト
        void AppendPostEffectPasses(const RenderContext& context);

        /// @brief BackBuffer パスへ最終入力論理リソースを設定する
        /// @param finalPostEffectResource PostEffect 後の最終論理リソース名
        void ConfigureBackBufferInput(const std::string& finalPostEffectResource);

        /// @brief Graph 実行後に最終表示テクスチャハンドルを同期する
        /// @param context レンダリングコンテキスト
        void SyncFinalDisplayHandle(const RenderContext& context);

        /// @brief 補助 RenderView 実行後の共有結果を収集する
        /// @param context 補助 View 実行に使用したレンダリングコンテキスト
        /// @return 補助 View の共有結果
        RenderViewResult BuildRenderViewResult(const RenderContext& context) const;

        /// @brief フェーズ順序管理付きのパス登録エントリ
        struct RenderPassEntry {
            std::unique_ptr<RenderPass> pass;
            RenderPassPhase phase = RenderPassPhase::Overlay;
            int priority = 0;
            uint64_t sequence = 0;     ///< 登録順（同フェーズ・同 priority の安定ソート用）
            const void* owner = nullptr; ///< nullptr = エンジン所有。シーン所有パスの一括除去に使う
        };

        std::vector<std::unique_ptr<PostEffectPass>> postEffectSubpasses_;
        /// @brief ポストエフェクト列へ渡す「シーンの画」の論理名（TAA / CAS 適用後）
        std::string sceneImageResourceName_ = FrameBlackboard::SceneColor;
        std::string finalDisplayResourceName_ = FrameBlackboard::SceneColor;

        std::vector<RenderPassEntry> passes_;
        uint64_t nextSequence_ = 0;
        const void* activeOwner_ = nullptr;
        RenderGraph renderGraph_{};
    };
}
