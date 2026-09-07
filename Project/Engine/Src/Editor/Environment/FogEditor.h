#pragma once

namespace CoreEngine {
    class EngineSystem;
    class FogManager;
    class GameDebugUI;

    /// @brief 高さフォグのエンジン常駐エディタ
    /// @details DebugSubsystem がエンジン寿命で 1 個所有し、どのシーンでも Environment ツリーから編集できる。
    ///          UI は「① プリセット → ② 詳細設定（CVar 自動生成）」の 2 層。
    class FogEditor {
    public:
        /// @brief 環境エディタの登録を解除する
        ~FogEditor();

        /// @brief 参照先を初期化し、環境エディタとして登録する
        void Initialize(EngineSystem& engine);

    private:
        /// @brief フォグの編集パネル内容を描画する（Inspector 内に埋め込み）
        void DrawContent();

        /// @brief プリセットボタン群を描画する（押した時点で即適用）
        void DrawPresetButtons();

        FogManager* GetFogManager() const;

        EngineSystem* engine_ = nullptr;

        /// @brief Initialize 時にキャッシュした GameDebugUI（デストラクタでの登録解除用）
        /// @details デストラクタで engine_->GetDebugSubsystem() を呼び直すと、サブシステム
        ///          一括破棄の途中で破棄済みサブシステムへ dynamic_cast することになり
        ///          アクセス違反になる。参照は Initialize 時に一度だけ取得してキャッシュする。
        GameDebugUI* gameDebugUI_ = nullptr;
    };
}
