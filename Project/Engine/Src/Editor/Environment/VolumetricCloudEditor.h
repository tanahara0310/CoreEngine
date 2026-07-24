#pragma once

namespace CoreEngine {
    class EngineSystem;
    class VolumetricCloudManager;
    class GameDebugUI;

    /// @brief ボリューメトリック雲のエンジン常駐エディタ
    /// @details 雲は全シーン既定の機能のため、シーン所有の facade ではなく DebugSubsystem が
    ///          エンジン寿命で 1 個所有し、どのシーンでも Hierarchy の Environment ツリーから
    ///          常に編集できるようにする。
    ///          UE の Volumetric Cloud を参考に「プリセット → 基本（少数の主要パラメータ）→
    ///          詳細（折りたたみ）」の3段構成で、開発時によく触る項目だけを前面に出す。
    class VolumetricCloudEditor {
    public:
        /// @brief 環境エディタの登録を解除する
        ~VolumetricCloudEditor();

        /// @brief 参照先を初期化し、環境エディタとして登録する
        void Initialize(EngineSystem& engine);

        /// @brief 現在適用中のプリセット index を取得（-1=カスタム）
        int GetActivePresetIndex() const { return activePresetIndex_; }

        /// @brief プリセット index を設定（エディタ設定の復元用。UI 表示状態のみで、パラメータは変更しない）
        void SetActivePresetIndex(int index) { activePresetIndex_ = index; }

    private:
        /// @brief 雲の編集パネル内容を描画する（Inspector 内に埋め込み）
        void DrawContent();

        /// @brief プリセット選択コンボと説明文を描画する（選択で即適用）
        void DrawPresetSelector(VolumetricCloudManager& manager);

        VolumetricCloudManager* GetVolumetricCloudManager() const;

        EngineSystem* engine_ = nullptr;

        /// @brief Initialize 時にキャッシュした GameDebugUI（デストラクタでの登録解除用）
        /// @details デストラクタで engine_->GetDebugSubsystem() を呼び直すと、この
        ///          エディタ自体を所有する DebugSubsystem が EngineSystem::Finalize() の
        ///          サブシステム一括破棄の途中（デストラクタ実行中）に自分自身を
        ///          dynamic_cast で探しに行くことになり、その時点で破棄済みの他サブシステムに
        ///          当たってアクセス違反になる（RTTI 読み取り不可 → std::terminate）。
        ///          そのため参照は Initialize 時に一度だけ取得してキャッシュする。
        GameDebugUI* gameDebugUI_ = nullptr;

        /// 現在適用中のプリセット（-1=カスタム。パラメータを手動変更すると自動でカスタムになる）
        int activePresetIndex_ = 1;
    };
}
