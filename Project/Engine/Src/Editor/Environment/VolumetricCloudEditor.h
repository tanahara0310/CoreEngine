#pragma once

namespace CoreEngine {
    class EngineSystem;
    class VolumetricCloudManager;
    class GameDebugUI;

    /// @brief ボリューメトリック雲のエンジン常駐エディタ
    /// @details DebugSubsystem がエンジン寿命で 1 個所有し、どのシーンでも Environment ツリーから編集できる。
    ///          UI は「⓪ スタイル → ① 天候（プリセット + メタスライダー）→ ② 配置ペイント
    ///          → ③ 詳細設定」の順。⓪ 以外は全スタイル共通で、③ だけスタイルで出し分ける。
    class VolumetricCloudEditor {
    public:
        /// @brief 環境エディタの登録を解除する
        ~VolumetricCloudEditor();

        /// @brief 参照先を初期化し、環境エディタとして登録する
        void Initialize(EngineSystem& engine);

    private:
        /// @brief 雲の編集パネル内容を描画する（Inspector 内に埋め込み）
        void DrawContent();

        /// @brief ⓪ スタイル選択コンボ（リアル / ブロック雲 3 種）
        void DrawStyleSelector();

        /// @brief プリセット選択コンボと説明文を描画する（選択で即適用）
        void DrawPresetSelector(VolumetricCloudManager& manager);

        /// @brief ① 天候メタスライダー（複数 CVar を意味単位で束ねたスライダー群）
        void DrawWeatherSliders();

        /// @brief ② 配置ペイント（ウェザーマップの上空視点表示 + ブラシ）
        void DrawPaintSection(VolumetricCloudManager& manager);

        /// @brief ③ 詳細設定（CVar を目的別グループの折りたたみで表示）
        void DrawAdvancedSection();

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

        /// 現在の値に一致するプリセット（-1=カスタム）。DrawPresetSelector が毎フレーム導出する
        int activePresetIndex_ = -1;

        // ===== 配置ペイントのブラシ状態 =====
        int paintTool_ = 0;             ///< 0=雲を置く / 1=晴れさせる / 2=ブラシ跡を消す
        float brushRadiusM_ = 4000.0f;  ///< ブラシ半径 [m]
        float brushStrength_ = 0.5f;    ///< ブラシ強さ [0,1]
        float typeTarget_ = 0.7f;       ///< 置く雲のタイプ（0=層雲〜1=積乱雲）
        float topTarget_ = 0.5f;        ///< 置く雲の雲頂高さ
        float mapSpanM_ = 0.0f;         ///< マップの表示範囲 [m]（0 で初回にペイント領域から決める）
        int mapChannel_ = 0;            ///< マップに表示する値（0=雲量 / 1=雲タイプ / 2=雲頂高さ）
        bool paintingStroke_ = false;   ///< ドラッグ中か（リリース時の自動保存判定）

        // ===== 連動スライダーの従 CVar の編集開始時の値（Undo バッチ用） =====
        float erosionOnEditStart_ = 0.0f;
        float windDirZOnEditStart_ = 0.0f;
    };
}
