#pragma once

#include <functional>

namespace CoreEngine
{
    class EngineSystem;
    class StartupSequence;
    struct EngineConfig;

    /// @brief グラフィックス関連コンポーネントの生成・初期化を担当するファクトリー
    /// @details EngineSystem::CreateGraphicsComponents の実装を分離し、
    ///　EngineSystem.h からグラフィックス系ヘッダーのインクルードを排除する。
    ///　EngineSystem の friend クラスとして RegisterComponent へアクセスする。
    class GraphicsComponentFactory
    {
    public:
        /// @brief グラフィックス関連コンポーネントの生成を起動シーケンスへステップとして積む
        /// @param sequence          積み先の起動シーケンス
        /// @param engine            登録対象の EngineSystem（シーケンス実行まで生存が必要）
        /// @param config            エンジン設定
        /// @param buildPreloadTasks ModelManager 生成直後に呼ばれる差し込み口（アセット先読み用）
        /// @note ここが起動時間の大半（シェーダ 100 本超のコンパイル）を占めるので、
        ///       スプラッシュが固まらないよう細かく切ってある。
        ///       ステップ間で受け渡す中間ポインタは共有状態として保持する。
        /// @note buildPreloadTasks は**シーケンスの組み立て時**に呼ばれる（実行時ではない）。
        ///       ここで積まれたステップはシェーダコンパイル群より前に実行されるので、
        ///       非同期の先読みを始めておけばコンパイル時間の裏に隠せる。
        static void BuildStartupTasks(
            StartupSequence& sequence,
            EngineSystem& engine,
            const EngineConfig& config,
            const std::function<void(StartupSequence&)>& buildPreloadTasks = {});
    };
}
