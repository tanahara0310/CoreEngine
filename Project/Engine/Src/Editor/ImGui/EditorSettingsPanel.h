#pragma once

#ifdef USE_IMGUI

/// @brief Engine Settings ウィンドウの「Editor Settings」パネル描画

namespace CoreEngine
{
    class EditorSettingsSubsystem;

    /// @brief エディタ設定自動保存の管理パネル
    /// @details 登録中セクションの一覧（状態・最終保存時刻）と、
    ///          リセット / バックアップ復元の操作 UI を描画する。
    ///          DebugSubsystem が RegisterEnginePanel から呼び出す。
    namespace EditorSettingsPanel
    {
        /// @brief パネル内容を描画する
        /// @param store 対象ストア（nullptr の場合は案内表示のみ）
        void Draw(EditorSettingsSubsystem* store);
    }
}

#endif // USE_IMGUI
