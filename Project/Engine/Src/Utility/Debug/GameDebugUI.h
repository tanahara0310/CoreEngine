#pragma once
#include "Editor/ImGui/Gizmo.h"
#ifdef USE_IMGUI
#include "Utility/Debug/ConsoleUI.h"
#include "Editor/ImGui/ScreenCapture.h"
#include "Editor/ImGui/PixCapture.h"
#endif
#include "Editor/ImGui/SceneManagerTab.h"
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

    /// @brief エンジンパネルのカテゴリ（表示先の分類）
    enum class EnginePanelCategory {
        Settings, ///< Engine Settings ウィンドウ内のセクション（設定系）
        Tools,    ///< Toolsメニューから開く独立ウィンドウ（作業用ツール。フローティング）
    };

    /// @brief ゲームデバッグUIクラス
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

        /// @brief エンジン専用デバッグパネルを登録
        /// @param label セクション / ウィンドウタイトルに表示する名前
        /// @param drawer コンテンツドロワー
        /// @param category Settings=Engine Settingsウィンドウのセクション / Tools=Toolsメニューから開くフローティングウィンドウ
        void RegisterEnginePanel(const std::string& label, std::function<void()> drawer,
            EnginePanelCategory category = EnginePanelCategory::Settings);

        /// @brief 環境エディタを登録（HierarchyのEnvironmentツリーに表示し、選択時にInspectorで編集）
        /// @param label Environmentツリーに表示する名前
        /// @param owner 登録元の識別子（登録元の破棄時に自分の登録だけを解除するために使う）
        /// @param drawer Inspector内に描画するコンテンツドロワー
        void RegisterEnvironmentEditor(const std::string& label, const void* owner, std::function<void()> drawer);

        /// @brief 環境エディタの登録を解除する（ownerが現在の登録元と一致する場合のみ）
        /// @param label 登録時のラベル
        /// @param owner 登録時に渡したowner
        void UnregisterEnvironmentEditor(const std::string& label, const void* owner);

        /// @brief EngineDebug メニュー専用パネルを登録（デバッグ情報カテゴリ用）
        /// @param label メニュー / ウィンドウタイトルに表示する名前
        /// @param drawer ウィンドウ内に描画するコンテンツドロワー
        void RegisterEngineDebugPanel(const std::string& label, std::function<void()> drawer);

        /// @brief 指定ラベルのエンジンパネルの表示状態を設定する（Toolsカテゴリの独立ウィンドウ用）
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

        /// @brief Gameビュー用のSceneDebugEditorを設定
        void SetSceneDebugEditor(SceneDebugEditor* sceneDebugEditor) { sceneDebugEditor_ = sceneDebugEditor; }

        /// @brief Gameビュー用のSceneDebugEditorを取得
        SceneDebugEditor* GetSceneDebugEditor() const { return sceneDebugEditor_; }

    private:
        EngineSystem* engine_ = nullptr;
        DockingUI* dockingUI_ = nullptr;

#ifdef USE_IMGUI
        std::unique_ptr<ConsoleUI> console_ = std::make_unique<ConsoleUI>();
#endif
        std::unique_ptr<SceneManagerTab> sceneManagerTab_ = std::make_unique<SceneManagerTab>();

        std::function<void()> hierarchyContentDrawer_;
        std::function<void()> inspectorObjectDrawer_;
        SceneDebugEditor* sceneDebugEditor_ = nullptr;

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
            EnginePanelCategory category = EnginePanelCategory::Settings;
        };
        std::vector<EnginePanelEntry> enginePanels_;

        /// @brief EngineDebug メニュー専用パネル（デバッグ情報カテゴリ、Debug メニューからトグル）
        std::vector<EnginePanelEntry> engineDebugPanels_;

        /// @brief 環境エディタ（HierarchyのEnvironmentツリー → Inspector表示）
        struct EnvironmentEntry {
            std::string label;
            const void* owner = nullptr;
            std::function<void()> drawer;
        };
        std::vector<EnvironmentEntry> environmentEditors_;
        std::string selectedEnvironmentLabel_; ///< Environmentツリーで選択中のエントリー（空=未選択）

        bool showHierarchy_ = true;
        bool showInspector_ = true;
        bool showConsole_ = true;
        bool showEngineSettings_ = false;   ///< Engine Settings ウィンドウの表示状態
        std::string selectedSettingsLabel_; ///< Engine Settings で選択中のセクション（空=未選択）
        char settingsFilter_[64] = {};      ///< Engine Settings のセクション検索文字列

#ifdef USE_IMGUI
        ScreenCapture screenCapture_;  ///< スクリーンキャプチャ機能
        PixCapture pixCapture_;  ///< PIX GPU キャプチャ機能
#endif

        static constexpr const char* consoleWindow = "Console";

    private:
        void ShowConsoleUI();
        void DrawHierarchyPanel();
        void DrawEnvironmentTree();
        const EnvironmentEntry* FindSelectedEnvironmentEntry() const;
        void DrawInspectorPanel();
        void DrawEnginePanels();
        void DrawEngineDebugPanels();
        void DrawEngineSettingsWindow();
        void RegisterWindowsForDocking();
    };
}
