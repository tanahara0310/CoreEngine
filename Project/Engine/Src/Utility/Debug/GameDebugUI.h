#pragma once
#include "Editor/ImGui/Gizmo.h"
#ifdef USE_IMGUI
#include "Utility/Debug/ConsoleUI.h"
#include "Editor/ImGui/ScreenCapture.h"
#include "Editor/ImGui/PixCapture.h"
#endif
#include "Editor/ImGui/SceneManagerTab.h"
#include "Editor/Panel/EditorPanelRegistry.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace CoreEngine
{

    class EngineSystem; // 前方宣言
    class DockingUI; // 前方宣言
    class SceneManager; // 前方宣言
    class SceneDebugEditor;
    class ProjectView;

    /// @brief エディタのメニューバーと常設パネルを描画するクラス。
    /// @details パネルの登録先は `Editor::EditorPanelRegistry` 一本。ここは
    ///          登録された記述子を placement 別に描き分けるだけを担う。
    class GameDebugUI {
    public:
        /// @brief 初期化
        /// @param engine エンジンシステム
        /// @param dockingUI ドッキングUI（ウィンドウ登録用）
        void Initialize(EngineSystem* engine, DockingUI* dockingUI = nullptr);

        /// @brief シーンマネージャーの設定
        /// @param sceneManager SceneManagerへのポインタ
        void SetSceneManager(SceneManager* sceneManager);

        /// @brief 更新
        void Update();

        /// @brief メニューバーのみを表示（ドッキング前に呼び出す）
        void ShowMainMenuBar();

        /// @brief メニューバー以外のデバッグパネルを表示
        void UpdateDebugPanels();

#ifdef USE_IMGUI
        /// @brief コンソールUIへのアクセッサ
        ConsoleUI* GetConsole() { return console_.get(); }
#endif

        /// @brief シーンマネージャータブへのアクセッサ
        SceneManagerTab* GetSceneManagerTab() { return sceneManagerTab_.get(); }

        /// @brief Gameビュー用のSceneDebugEditorを設定
        void SetSceneDebugEditor(SceneDebugEditor* sceneDebugEditor) { sceneDebugEditor_ = sceneDebugEditor; }

        /// @brief Gameビュー用のSceneDebugEditorを取得
        SceneDebugEditor* GetSceneDebugEditor() const { return sceneDebugEditor_; }

        /// @brief ゲーム画面のみを表示する独立ウィンドウを開いているか
        /// @details エディタUIを一切含まない、Release ビルド相当の見た目を確認するためのウィンドウ。
        bool IsStandaloneGameWindowVisible() const { return showStandaloneGameWindow_; }

        /// @brief ゲーム画面のみの独立ウィンドウの表示状態を設定する
        /// @param visible 表示するなら true
        void SetStandaloneGameWindowVisible(bool visible) { showStandaloneGameWindow_ = visible; }

        /// @brief 上下のバーに出す状態を集めてドッキングUIへ渡す
        /// @note パネルを描き始める前に呼ぶ（ツールバーとステータスバーが同じ値を出すため）。
        void RefreshEditorStatus();

    private:
        EngineSystem* engine_ = nullptr;
        DockingUI* dockingUI_ = nullptr;
        SceneManager* sceneManager_ = nullptr;

#ifdef USE_IMGUI
        std::unique_ptr<ConsoleUI> console_ = std::make_unique<ConsoleUI>();
#endif
        std::unique_ptr<SceneManagerTab> sceneManagerTab_ = std::make_unique<SceneManagerTab>();

        SceneDebugEditor* sceneDebugEditor_ = nullptr;

        std::string selectedEnvironmentLabel_; ///< Environmentツリーで選択中のエントリー（空=未選択）

        bool showHierarchy_ = true;
        bool showInspector_ = true;
        bool showConsole_ = true;
        bool showStandaloneGameWindow_ = false; ///< ゲーム画面のみの独立ウィンドウ
        bool showEngineSettings_ = false;   ///< Engine Settings ウィンドウの表示状態
        bool showAboutWindow_ = false;      ///< バージョン情報ウィンドウの表示状態
        std::string selectedSettingsLabel_; ///< Engine Settings で選択中のセクション（空=未選択）
        char settingsFilter_[64] = {};      ///< Engine Settings のセクション検索文字列

#ifdef USE_IMGUI
        ScreenCapture screenCapture_;  ///< スクリーンキャプチャ機能
        PixCapture pixCapture_;  ///< PIX GPU キャプチャ機能
#endif

        static constexpr const char* consoleWindow = "Console";

    private:
        void ShowConsoleUI();

        /// @brief メニューバーの各メニュー
        void DrawFileMenu();
        void DrawEditMenu();
        void DrawComponentMenu();
        void DrawWindowMenu();
        void DrawHelpMenu();

        /// @brief メニューバー右端のシーン名とビルド構成
        void DrawMenuBarChips();

        /// @brief グローバルなショートカット（再生・レイアウト）
        void HandleShortcuts();

        /// @brief バージョン情報のウィンドウ
        void DrawAboutWindow();

        /// @brief プロジェクトビュー（取れなければ nullptr）
        ProjectView* FindProjectView() const;

        /// @brief Window メニュー内の 1 グループをサブメニューとして描画する
        /// @param extraContent 省略可。グループ固有の追加項目（区切り線の後に描画される）
        /// @note 該当が 1 件も無いグループは項目自体を出さない
        void DrawPanelGroupMenu(Editor::PanelGroup group, const char* label,
            const std::function<void()>& extraContent = nullptr);

        void DrawHierarchyPanel();
        void DrawEnvironmentTree();
        Editor::EditorPanel* FindSelectedEnvironmentEntry();
        void DrawInspectorPanel();
        void DrawPanelWindows();
        void DrawEngineSettingsWindow();
        void RegisterWindowsForDocking();
    };
}
