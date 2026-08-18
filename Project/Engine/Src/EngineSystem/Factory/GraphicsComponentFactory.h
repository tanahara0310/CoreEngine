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
    /// @details EngineSystem::CreateGraphicsComponents の実装を分離し、
    ///　EngineSystem.h からグラフィックス系ヘッダーのインクルードを排除する。
    ///　EngineSystem の friend クラスとして RegisterComponent へアクセスする。
    ///
    /// @details 2 関数に分かれているのは、**間にアセット先読みを挟むため**。
    ///          呼び出し側（EngineSystem::BuildStartupTasks）が
    ///          「土台 → ゲームの先読み → レンダラー群」と一列に並べる。
    ///          以前は先読みフックを std::function でこの中へ差し込んでいたが、
    ///          挿入位置が factory の実装を読まないと分からなかったため分割した。
    class GraphicsComponentFactory
    {
    public:
        /// @brief デバイスとアセットロード土台の生成ステップを積む
        /// @details 内訳: DirectX12 デバイス → TextureManager / ResourceFactory / ModelManager。
        ///          ここまで実行されればモデル・テクスチャの先読みを開始できる
        ///          （＝この直後に積んだ先読みステップは、後続のレンダラー群の
        ///          シェーダコンパイル数秒の裏に実処理を隠せる）。
        /// @return BuildRendererTasks へ渡す共有状態
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
