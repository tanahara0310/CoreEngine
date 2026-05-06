#pragma once
#include "Utility/Debug/ImGui/Gizmo.h"
#ifdef USE_IMGUI
#include "Utility/Debug/ConsoleUI.h"
#include "Utility/Debug/ImGui/ScreenCapture.h"
#include "Utility/Debug/ImGui/PixCapture.h"
#endif
#include "Utility/Debug/ImGui/SceneManagerTab.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

/// @brief デバッグ用のUIクラス
/// エンジンシステムの低レベル情報の表示とデバッグ支援を行う

namespace CoreEngine
{

    class EngineSystem; // 前方宣言
    class DockingUI; // 前方宣言
    class SceneManager; // 前方宣言

    class GameDebugUI {
    public:
        /// @brief 初期化
        /// @param engine エンジンシステム
        /// @param dockingUI ドッキングUI（ウィンドウ登録用）
        void Initialize(EngineSystem* engine, DockingUI* dockingUI = nullptr);

        /// @brief シーンマネージャーの設定
        /// @param sceneManager SceneManagerへのポインタ
        void SetSceneManager(SceneManager* sceneManager);

        /// @brief Hierarchyパネル用コンテンツドロワーを設定（SceneDebugEditorから登録）
        void SetHierarchyContentDrawer(std::function<void()> callback) { hierarchyContentDrawer_ = std::move(callback); }

        /// @brief Camera EditorをEngine Editorに登録（SceneDebugEditorから登録）
        void SetInspectorCameraDrawer(std::function<void()> callback);

        /// @brief Inspectorの選択オブジェクトドロワーを設定（SceneDebugEditorから登録）
        void SetInspectorObjectDrawer(std::function<void()> callback) { inspectorObjectDrawer_ = std::move(callback); }

        /// @brief アプリケーション固有のエディターをApp Editorに登録
        /// @param label メニューに表示する名前
        /// @param drawer Inspector内に描画するコンテンツドロワー
        void RegisterAppEditor(const std::string& label, std::function<void()> drawer);

        /// @brief エンジンデバッグ情報をInspectorタブとして登録（EngineDebugメニューからトグル）
        /// @param label メニュー / タブ名
        /// @param drawer タブ内に描画するコンテンツドロワー
        void RegisterEngineEditor(const std::string& label, std::function<void()> drawer);

        /// @brief エンジン専用デバッグパネルを登録（Engineメニュー → 独立ウィンドウ）
        /// @param label メニュー / ウィンドウタイトルに表示する名前
        /// @param drawer ウィンドウ内に描画するコンテンツドロワー
        void RegisterEnginePanel(const std::string& label, std::function<void()> drawer);

        /// @brief EngineDebug メニュー専用パネルを登録（デバッグ情報カテゴリ用）
        /// @param label メニュー / ウィンドウタイトルに表示する名前
        /// @param drawer ウィンドウ内に描画するコンテンツドロワー
        void RegisterEngineDebugPanel(const std::string& label, std::function<void()> drawer);

        /// @brief 指定ラベルのエンジンパネルの表示状態を設定する
        /// @param label パネルのラベル
        /// @param visible 表示するならtrue
        void SetPanelVisible(const std::string& label, bool visible);

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

    private:
        EngineSystem* engine_ = nullptr;
        DockingUI* dockingUI_ = nullptr;

#ifdef USE_IMGUI
        std::unique_ptr<ConsoleUI> console_ = std::make_unique<ConsoleUI>();
#endif
        std::unique_ptr<SceneManagerTab> sceneManagerTab_ = std::make_unique<SceneManagerTab>();

        std::function<void()> hierarchyContentDrawer_;
        std::function<void()> inspectorObjectDrawer_;

        /// @brief エディターエントリー（Inspectorタブとして表示）
        struct EditorEntry {
            std::string label;
            std::function<void()> drawer;
            bool visible = false;  ///< Inspectorのタブバーに表示するか
        };
        using EditorList = std::vector<EditorEntry>;
        EditorList engineEditors_;  ///< エンジンエディター（Camera Editor等）
        EditorList appEditors_;     ///< アプリエディター

        /// @brief エンジン専用デバッグパネル（独立ウィンドウで描画）
        struct EnginePanelEntry {
            std::string label;
            std::function<void()> drawer;
            bool visible = false;
        };
        std::vector<EnginePanelEntry> enginePanels_;

        /// @brief EngineDebug メニュー専用パネル（デバッグ情報カテゴリ、Window Manager には出ない）
        std::vector<EnginePanelEntry> engineDebugPanels_;

        bool showConsole_ = true;
        bool showEditorSwitcher_ = false;  ///< Window Managerパネルの表示状態
        bool showEngineDebugMenu_ = false; ///< EngineDebug メニューの展開状態（未使用・予約）

#ifdef USE_IMGUI
        ScreenCapture screenCapture_;  ///< スクリーンキャプチャ機能
        PixCapture pixCapture_;  ///< PIX GPU キャプチャ機能
#endif

        static constexpr const char* consoleWindow = "Console";

    private:
        void ShowConsoleUI();
        void DrawHierarchyPanel();
        void DrawInspectorPanel();
        void DrawEnginePanels();
        void DrawEngineDebugPanels();
        void DrawEditorSwitcherPanel();
        void RegisterWindowsForDocking();
    };
}
