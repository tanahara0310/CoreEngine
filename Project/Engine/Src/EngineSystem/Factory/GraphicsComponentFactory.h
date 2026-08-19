#pragma once

#include <memory>

namespace CoreEngine
{
    class EngineSystem;
    class StartupSequence;
    struct EngineConfig;

    /// @brief グラフィックス初期化のステップ間で受け渡す中間ポインタ（実体は .cpp）
    /// @details 呼び出し側からは不透明。BuildFoundationTasks の戻り値を
    ///          そのまま BuildRendererTasks へ渡すためだけに存在する。
    struct GraphicsSetupState;

    /// @brief グラフィックス関連コンポーネントの生成・初期化を担当するファクトリー
    /// @details EngineSystem.h からグラフィックス系ヘッダーの include を排除するために分離した。
    /// @note 2 関数に分かれているのは、間にゲーム側のアセット先読みを挟むため。
    class GraphicsComponentFactory
    {
    public:
        /// @brief デバイスとアセットロード土台（TextureManager / ResourceFactory / ModelManager）の生成ステップを積む
        /// @return BuildRendererTasks へ渡す共有状態
        /// @note ここまで実行されればモデル・テクスチャの先読みを開始できる
        static std::shared_ptr<GraphicsSetupState> BuildFoundationTasks(
            StartupSequence& sequence, EngineSystem& engine, const EngineConfig& config);

        /// @brief レンダラー群・ポストエフェクト・レンダリング技術・IBL のステップを積む
        /// @param state BuildFoundationTasks が返した共有状態
        /// @note ここが起動時間の大半（シェーダ 100 本超のコンパイル）を占めるので、
        ///       スプラッシュが固まらないよう細かく切ってある。
        static void BuildRendererTasks(
            StartupSequence& sequence, EngineSystem& engine,
            std::shared_ptr<GraphicsSetupState> state);
    };
}
