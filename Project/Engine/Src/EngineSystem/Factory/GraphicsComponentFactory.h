#pragma once

namespace CoreEngine
{
    class EngineSystem;
    struct EngineConfig;

    /// @brief グラフィックス関連コンポーネントの生成・初期化を担当するファクトリー
    /// @details EngineSystem::CreateGraphicsComponents の実装を分離し、
    ///　EngineSystem.h からグラフィックス系ヘッダーのインクルードを排除する。
    ///　EngineSystem の friend クラスとして RegisterComponent へアクセスする。
    class GraphicsComponentFactory
    {
    public:
        /// @brief グラフィックス関連コンポーネントを一括生成・登録する
        /// @param engine 登録対象の EngineSystem
        /// @param config エンジン設定
        static void Setup(EngineSystem& engine, const EngineConfig& config);
    };
}
