#pragma once
#include "RenderPass.h"
#include "PostEffectPass.h"
#include "Graphics/Render/RenderGraph.h"
#include "Graphics/Render/RenderGraphSnapshot.h"
#include <cstdint>
#include <vector>
#include <memory>
#include <optional>
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
        /// @param timingCategoryOverride タイミング表示カテゴリの上書き（nullptr = phase から機械的に決定）
        ///        実行順の都合で置いたフェーズと、計測上の分類が食い違うパス
        ///        （例: 波形生成は PostLighting に置くが、コストの分類としては Water）に使う。
        /// @return 追加したパスへのポインタ（RemovePass 用ハンドル）
        RenderPass* AddPass(
            std::unique_ptr<RenderPass> pass,
            RenderPassPhase phase,
            int priority = 0,
            std::optional<GpuTimingCategory> timingCategoryOverride = std::nullopt);

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

        /// @brief 今フレームのビュー（ViewInfo）を確定する
        /// @details 「どのカメラで描くか」を解決する唯一の場所。TAA の射影ジッタを注入してから
        ///          スナップショットするため、これ以降フレーム内の全パス・全描画が同じ行列を使う。
        ///          RenderGraph 構築より前、かつ RenderContext::frameViews を差す前に呼ぶこと。
        /// @param context レンダリングコンテキスト（frameViews はまだ未設定でよい）
        /// @param outViews 構築先
        void PrepareFrameViews(const RenderContext& context, FrameViews& outViews);

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

        // ── RenderGraph エディタ向けスナップショット ──────────────────────────────

        /// @brief Graph の構築・実行結果をフレームごとに複製するかを設定する
        /// @details 無効時は RenderGraph の計装も切れ、記録コストはゼロになる。
        ///          エディタ側がウィンドウの表示状態に合わせて毎フレーム設定する想定。
        /// @param enabled 複製する場合 true
        void SetGraphCaptureEnabled(bool enabled)
        {
            graphCaptureEnabled_ = enabled;
            renderGraph_.SetInstrumentationEnabled(enabled);
            if (!enabled) {
                graphSnapshots_.clear();
            }
        }

        /// @brief スナップショット複製が有効かを返す
        bool IsGraphCaptureEnabled() const { return graphCaptureEnabled_; }

        /// @brief スナップショットの更新を止める（今の内容を保持したまま眺めるため）
        /// @param paused 停止する場合 true
        void SetGraphCapturePaused(bool paused) { graphCapturePaused_ = paused; }

        /// @brief スナップショット更新が停止中かを返す
        bool IsGraphCapturePaused() const { return graphCapturePaused_; }

        /// @brief 直近フレームの View 別スナップショットを取得する
        /// @return 実行順に並んだ View 別スナップショット（補助 View → GameView の順）
        const std::vector<RenderGraphSnapshot>& GetGraphSnapshots() const { return graphSnapshots_; }

    private:
        /// @brief 現在の Graph 構築・実行結果を 1 View 分スナップショットへ複製する
        /// @param context この View の実行に使ったレンダリングコンテキスト
        void CaptureGraphSnapshot(const RenderContext& context);

        /// @brief 保持中のスナップショットを破棄する
        /// @details パス実体が消える操作（RemovePass 等）の直後に必ず呼ぶこと。
        ///          スナップショットは RenderPass* を握っており、放置すると
        ///          ポーズ中の参照が解放済みメモリを指す。
        void InvalidateGraphSnapshots()
        {
            graphSnapshots_.clear();
            graphSnapshotFrameNumber_ = UINT64_MAX;
        }

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
            std::optional<GpuTimingCategory> timingCategoryOverride; ///< 未設定なら phase から決定
        };

        std::vector<std::unique_ptr<PostEffectPass>> postEffectSubpasses_;
        /// @brief ポストエフェクト列へ渡す「シーンの画」の論理名（TAA / CAS 適用後）
        std::string sceneImageResourceName_ = FrameBlackboard::SceneColor;
        std::string finalDisplayResourceName_ = FrameBlackboard::SceneColor;

        std::vector<RenderPassEntry> passes_;
        uint64_t nextSequence_ = 0;
        const void* activeOwner_ = nullptr;
        RenderGraph renderGraph_{};

        std::vector<RenderGraphSnapshot> graphSnapshots_;
        uint64_t graphSnapshotFrameNumber_ = UINT64_MAX; ///< 今 graphSnapshots_ に溜めているフレーム
        bool graphCaptureEnabled_ = false;
        bool graphCapturePaused_ = false;
    };
}
