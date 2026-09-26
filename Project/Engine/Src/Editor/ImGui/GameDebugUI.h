#pragma once
#include "Editor/ImGui/Gizmo.h"
#ifdef CORE_EDITOR
#include "Editor/ImGui/ConsoleUI.h"
#include "Graphics/RHI/Debug/PixCapture.h"
#include "WinApp/ScreenCapture.h"
#include "Editor/ImGui/ProjectSettingsWindow.h"
#include "Editor/Launcher/ProjectBrowser.h"
#endif
#include "Editor/Panel/EditorPanelRegistry.h"
#include <filesystem>
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

    namespace Editor
    {
        class PlayModeController;
    }

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

#ifdef CORE_EDITOR
        /// @brief コンソールUIへのアクセッサ
        ConsoleUI* GetConsole() { return console_.get(); }
#endif

        /// @brief Gameビュー用のSceneDebugEditorを設定
        void SetSceneDebugEditor(SceneDebugEditor* sceneDebugEditor) { sceneDebugEditor_ = sceneDebugEditor; }

        /// @brief 常設ウィンドウ（Hierarchy・Inspector・Console）の開閉
        struct CoreWindows
        {
            bool hierarchy = true;
            bool inspector = true;
            bool console = true;
        };
        CoreWindows GetCoreWindows() const { return { showHierarchy_, showInspector_, showConsole_ }; }
        void SetCoreWindows(const CoreWindows& windows)
        {
            showHierarchy_ = windows.hierarchy;
            showInspector_ = windows.inspector;
            showConsole_ = windows.console;
        }

        /// @brief Gameビュー用のSceneDebugEditorを取得
        SceneDebugEditor* GetSceneDebugEditor() const { return sceneDebugEditor_; }

        /// @brief 再生の前の控えを持つ相手を設定する（上下のバーに控えの様子を出す）
        void SetPlayModeController(Editor::PlayModeController* controller) { playModeController_ = controller; }

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
        /// @brief 保存していない変更を確かめてから行う操作
        enum class PendingSceneAction {
            None,
            Open,          ///< 別のシーンを開く
            Create,        ///< 新しいシーンを作って開く
            SwitchProject, ///< 別のプロジェクトを開く（エディタを起動し直す）
        };

        EngineSystem* engine_ = nullptr;
        DockingUI* dockingUI_ = nullptr;
        SceneManager* sceneManager_ = nullptr;

#ifdef CORE_EDITOR
        std::unique_ptr<ConsoleUI> console_ = std::make_unique<ConsoleUI>();
#endif

        SceneDebugEditor* sceneDebugEditor_ = nullptr;
        Editor::PlayModeController* playModeController_ = nullptr;


        bool showHierarchy_ = true;
        bool showInspector_ = true;
        bool showConsole_ = true;
        bool showStandaloneGameWindow_ = false; ///< ゲーム画面のみの独立ウィンドウ
        bool showProjectSettings_ = false;  ///< Project Settings ウィンドウの表示状態
        bool showAboutWindow_ = false;      ///< バージョン情報ウィンドウの表示状態

        bool showNewSceneDialog_ = false;   ///< 新しいシーンの窓の表示状態
        char newSceneName_[64] = {};        ///< 新しいシーンの名前
        int  newSceneTemplate_ = 1;         ///< ひな形（0: 空 / 1: 基本）
        std::string newSceneError_;         ///< 名前が使えないときの理由（空なら出さない）

        PendingSceneAction pendingSceneAction_ = PendingSceneAction::None;
        std::string pendingSceneName_;      ///< 開く／作るシーンの名前
        int pendingSceneTemplate_ = 1;      ///< 作るときのひな形
        bool lastScriptOk_ = true;          ///< 前のフレームでスクリプトのコンパイルが通っていたか

#ifdef CORE_EDITOR
        ScreenCapture screenCapture_;  ///< スクリーンキャプチャ機能
        PixCapture pixCapture_;  ///< PIX GPU キャプチャ機能
        ProjectSettingsWindow projectSettings_;  ///< Project Settings ウィンドウ

        Editor::ProjectList projectList_;                        ///< 最近のプロジェクトの一覧
        std::unique_ptr<Editor::ProjectBrowser> projectBrowser_; ///< プロジェクトの窓の中身（開いている間だけある）
        std::vector<Editor::ProjectEntry> recentProjects_;       ///< 最近のプロジェクトのメニューに出すもの
        std::filesystem::path pendingProjectFolder_;             ///< 確かめたあとに開くプロジェクト
#endif

        static constexpr const char* consoleWindow = "Console";

    private:
        void ShowConsoleUI();

        /// @brief メニューバーの各メニュー
        void DrawFileMenu();
        void DrawEditMenu();
        void DrawGameObjectMenu();
        void DrawAssetsMenu();
        void DrawComponentMenu();
        void DrawWindowMenu();
        void DrawHelpMenu();

        /// @brief メニューバー右端の再生モード・プロジェクト名・シーン名・ビルド構成
        void DrawMenuBarChips();

        /// @brief グローバルなショートカット（再生・一時停止・コマ送り・レイアウト）
        void HandleShortcuts();

        /// @brief バージョン情報のウィンドウ
        void DrawAboutWindow();

        /// @brief 新しいシーンの窓（名前とひな形を決めて作る）
        void DrawNewSceneDialog();

        /// @brief 保存していない変更があるときの確認の窓
        void DrawUnsavedChangesDialog();

        /// @brief シーンを開く（保存していない変更があれば先に確認する）
        void RequestOpenScene(const std::string& name);

        /// @brief 新しいシーンを作って開く（保存していない変更があれば先に確認する）
        void RequestCreateScene(const std::string& name, int templateIndex);

        /// @brief 確認で「続ける」を選んだときに、待たせていた操作を行う
        void RunPendingSceneAction();

        /// @brief 新しいシーンを作って登録し、開く
        /// @return 作れたら true（作れなければ newSceneError_ に理由が入る）
        bool CreateAndOpenScene(const std::string& name, int templateIndex);

        /// @brief プロジェクトの窓（一覧と新規作成）を開く
        /// @param newProject 新規作成の画面から始めるなら true
        void OpenProjectBrowser(bool newProject);

        /// @brief プロジェクトの窓
        void DrawProjectBrowser();

        /// @brief 「最近のプロジェクト」のサブメニューの中身
        void DrawRecentProjectsMenu();

        /// @brief エディタを folder のプロジェクトで起動し直す（保存していない変更があれば先に確認する）
        void RequestSwitchProject(const std::filesystem::path& folder);

        /// @brief プロジェクトビュー（取れなければ nullptr）
        ProjectView* FindProjectView() const;

        /// @brief Window メニュー内の 1 グループをサブメニューとして描画する
        /// @param extraContent 省略可。グループ固有の追加項目（区切り線の後に描画される）
        /// @note 該当が 1 件も無いグループは項目自体を出さない
        void DrawPanelGroupMenu(Editor::PanelGroup group, const char* label,
            const std::function<void()>& extraContent = nullptr);

        void DrawHierarchyPanel();
        void DrawInspectorPanel();
        void DrawPanelWindows();
        void RegisterWindowsForDocking();
    };
}
