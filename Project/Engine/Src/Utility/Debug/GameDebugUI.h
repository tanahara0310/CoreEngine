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
#include <utility>
#include <vector>

namespace CoreEngine
{

    class EngineSystem; // 前方宣言
    class DockingUI; // 前方宣言
    class SceneManager; // 前方宣言
    class SceneDebugEditor;

    /// @brief エンジンパネルのカテゴリ（＝どこに描画されるか）
    enum class EnginePanelCategory {
        Settings, ///< Engine Settings ウィンドウ内のセクション（設定系）
        Tools,    ///< 単独ウィンドウとして開くパネル（フローティング / ドック可能）
    };

    /// @brief メニュー上での分類（＝どこから開くか）
    /// @details Unity の Window メニューと同じく、パネルが増えても一覧が伸び続けないよう
    ///          用途ごとのサブメニューへ振り分けるための分類。
    ///          Category（描画先）とは独立した軸なので、両方を指定する。
    enum class EnginePanelGroup {
        General,   ///< 常設パネル（Hierarchy / Inspector / Console など）
        Rendering, ///< 描画結果の確認・レンダリング設定
        Analysis,  ///< 計測・統計・プロファイリング
        Editor,    ///< エディタ自身の設定・操作
    };

    /// @brief グループの表示順と表示名
    /// @details Window メニューと Engine Settings の一覧で同じ順序・同じ名前を使うため、
    ///          並びをここ 1 か所に持たせる。グループを増やしたらここへ追記する。
    inline constexpr std::pair<EnginePanelGroup, const char*> kEnginePanelGroupOrder[] = {
        { EnginePanelGroup::General,   "General"   },
        { EnginePanelGroup::Rendering, "Rendering" },
        { EnginePanelGroup::Analysis,  "Analysis"  },
        { EnginePanelGroup::Editor,    "Editor"    },
    };

    /// @brief グループの表示名を返す
    /// @param group 対象グループ
    /// @return 表示名
    inline const char* ToDisplayName(EnginePanelGroup group)
    {
        for (const auto& [value, label] : kEnginePanelGroupOrder) {
            if (value == group) {
                return label;
            }
        }
        return "Other";
    }

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
        /// @param category Settings=Engine Settingsウィンドウのセクション / Tools=単独ウィンドウ
        /// @param group Window メニューのどのサブメニューへ載せるか（Tools のときのみ使用）
        void RegisterEnginePanel(const std::string& label, std::function<void()> drawer,
            EnginePanelCategory category = EnginePanelCategory::Settings,
            EnginePanelGroup group = EnginePanelGroup::General);

        /// @brief 環境エディタを登録（Hierarchy の Environment ツリーに表示し、Inspector で編集）
        /// @param owner 登録元の識別子（登録元の破棄時に自分の登録だけを解除するために使う）
        /// @param childTree 省略可。子ツリー行のドロワー（クリックされたら true を返すこと）
        /// @param onParentSelected 省略可。親エントリ自体が選択されたときに呼ばれる
        void RegisterEnvironmentEditor(const std::string& label, const void* owner, std::function<void()> drawer,
            std::function<bool()> childTree = nullptr, std::function<void()> onParentSelected = nullptr);

        /// @brief 環境エディタの登録を解除する（ownerが現在の登録元と一致する場合のみ）
        /// @param label 登録時のラベル
        /// @param owner 登録時に渡したowner
        void UnregisterEnvironmentEditor(const std::string& label, const void* owner);

        /// @brief デバッグ情報パネルを登録（単独ウィンドウ）
        /// @param label メニュー / ウィンドウタイトルに表示する名前
        /// @param drawer ウィンドウ内に描画するコンテンツドロワー
        /// @param group Window メニューのどのサブメニューへ載せるか
        void RegisterEngineDebugPanel(const std::string& label, std::function<void()> drawer,
            EnginePanelGroup group = EnginePanelGroup::Analysis);

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

        /// @brief ゲーム画面のみを表示する独立ウィンドウを開いているか
        /// @details エディタUIを一切含まない、Release ビルド相当の見た目を確認するためのウィンドウ。
        bool IsStandaloneGameWindowVisible() const { return showStandaloneGameWindow_; }

        /// @brief ゲーム画面のみの独立ウィンドウの表示状態を設定する
        /// @param visible 表示するなら true
        void SetStandaloneGameWindowVisible(bool visible) { showStandaloneGameWindow_ = visible; }

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
            EnginePanelGroup group = EnginePanelGroup::General;
        };
        std::vector<EnginePanelEntry> enginePanels_;

        /// @brief EngineDebug メニュー専用パネル（デバッグ情報カテゴリ、Debug メニューからトグル）
        std::vector<EnginePanelEntry> engineDebugPanels_;

        /// @brief 環境エディタ（HierarchyのEnvironmentツリー → Inspector表示）
        struct EnvironmentEntry {
            std::string label;
            const void* owner = nullptr;
            std::function<void()> drawer;
            std::function<bool()> childTree;        ///< 子ツリー行の描画（クリックで true。無ければ葉エントリ）
            std::function<void()> onParentSelected; ///< 親エントリ選択時の通知（子側の選択解除用）
        };
        std::vector<EnvironmentEntry> environmentEditors_;
        std::string selectedEnvironmentLabel_; ///< Environmentツリーで選択中のエントリー（空=未選択）

        bool showHierarchy_ = true;
        bool showInspector_ = true;
        bool showConsole_ = true;
        bool showStandaloneGameWindow_ = false; ///< ゲーム画面のみの独立ウィンドウ
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

        /// @brief メニューバー中央へ再生 / 停止ボタンを描画する
        /// @details 切り替え先は PlaybackStateManager。停止中はゲームの更新だけが止まり、
        ///          エディタ UI とパラメータ編集はそのまま使える。
        void DrawPlaybackControls();

        /// @brief Window メニュー内の 1 グループをサブメニューとして描画する
        /// @param extraContent 省略可。グループ固有の追加項目（区切り線の後に描画される）
        /// @note 該当が 1 件も無いグループは項目自体を出さない
        void DrawPanelGroupMenu(EnginePanelGroup group, const char* label,
            const std::function<void()>& extraContent = nullptr);

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
