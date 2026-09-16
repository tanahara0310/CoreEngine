#include "pch.h"
#include "GameDebugUI.h"

#ifdef USE_IMGUI
#include "Editor/Command/EditorCommandStack.h"
#include "Editor/ImGui/DockingUI.h"
#include "Editor/ImGui/EditorTheme.h"
#include "Editor/ImGui/ImGuiManager.h"
#include "Editor/ImGui/ProjectView.h"
#include "Editor/ImGui/Widgets/EditorBars.h"
#include "Editor/Panel/EditorPanelRegistry.h"
#include "Editor/Scene/ComponentEditing.h"
#include "Editor/Scene/SceneDebugEditor.h"
#include "EngineSystem/EngineSystem.h"
#include "EngineSystem/EngineConfig.h"
#include "EngineSystem/PlaybackState.h"
#include "GameObject/Component/Core/ComponentFactory.h"
#include "Scene/SceneManager.h"
#include "Script/ScriptSubsystem.h"
#include "Utility/FrameRate/FrameRateController.h"
#include "Utility/FrameRate/Time.h"
#include "WinApp/WinApp.h"
#include <imgui.h>


namespace CoreEngine
{
    namespace
    {
        /// @brief 実行中のビルド構成の名前
        constexpr const char* BuildConfigName()
        {
#if defined(_DEBUG)
            return "Editor · Debug";
#elif defined(NDEBUG)
            return "Editor · Release";
#else
            return "Editor · Development";
#endif
        }
    }

    void GameDebugUI::Initialize(EngineSystem* engine, DockingUI* dockingUI)
    {
        assert(engine != nullptr);
        engine_ = engine;
        dockingUI_ = dockingUI;

        console_->Initialize();
        console_->SetEngineSystem(engine);

        // スクリーンキャプチャにHWNDを設定
        if (auto* debug = engine->GetDebugSubsystem()) {
            screenCapture_.SetHwnd(debug->GetImGuiManager()->GetHwnd());
        }

        if (dockingUI_) {
            RegisterWindowsForDocking();
        }

        // 単独ウィンドウのドック先は記述子の defaultDock だけが決める。
        // 既定は None なので、指定の無いパネルはフローティングのまま Window メニューから開く。
        // レジストリへ既に積まれているぶんもここでまとめて通知される。
        Editor::EditorPanelRegistry::Get().SetDockRegistrar(
            [this](const Editor::EditorPanelDesc& desc) {
                if (dockingUI_) {
                    dockingUI_->RegisterWindow(desc.id, desc.defaultDock);
                }
            });

        console_->LogInfo("GameDebugUIが正常に初期化されました");
        console_->LogDebug("エンジンシステムが正常に接続されました");
    }

    void GameDebugUI::SetSceneManager(SceneManager* sceneManager)
    {
        sceneManager_ = sceneManager;
        if (sceneManager) {
            sceneManagerTab_->Initialize(sceneManager);
            console_->LogInfo("SceneManagerがSceneManagerTabに設定されました");
        }
    }

    void GameDebugUI::Update()
    {
        // メニューバーと他のパネルをまとめて呼び出す
        ShowMainMenuBar();
        UpdateDebugPanels();
    }

    void GameDebugUI::ShowMainMenuBar()
    {
        // 前フレームでリクエストされたキャプチャを処理
        screenCapture_.ProcessPendingCapture();
        pixCapture_.ProcessPendingCapture();

        if (!ImGui::BeginMainMenuBar()) {
            return;
        }

        ImGui::TextColored(Editor::Theme::kAccentHover, "CoreEngine");
        ImGui::Dummy(ImVec2(6.0f, 0.0f));

        DrawFileMenu();
        DrawEditMenu();
        DrawComponentMenu();
        DrawWindowMenu();
        DrawHelpMenu();
        DrawMenuBarChips();

        ImGui::EndMainMenuBar();
    }

    void GameDebugUI::DrawFileMenu()
    {
        if (!ImGui::BeginMenu("File")) {
            return;
        }

        const bool hasScene = sceneDebugEditor_ && !sceneDebugEditor_->GetSceneName().empty();
        if (ImGui::MenuItem("シーンを保存", "Ctrl+S", false, hasScene)) {
            sceneDebugEditor_->SaveScene();
        }

        if (sceneManager_ && ImGui::BeginMenu("シーンを開く")) {
            const std::string current = sceneManager_->GetCurrentSceneName();
            for (const std::string& name : sceneManager_->GetAllSceneNames()) {
                if (ImGui::MenuItem(name.c_str(), nullptr, name == current)) {
                    sceneManager_->ChangeScene(name);
                }
            }
            ImGui::EndMenu();
        }

        ImGui::Separator();

        if (ImGui::MenuItem("スクリーンショット")) {
            screenCapture_.RequestCapture();
        }

        if (PixCapture::IsPixAvailable()) {
            if (ImGui::MenuItem("PIX GPU キャプチャ")) {
                pixCapture_.RequestCapture();
            }
            if (ImGui::MenuItem("PIX を無効化して再起動")) {
                EngineConfig::SetPixRuntimeAndRestart(false);
            }
        } else {
            ImGui::MenuItem("PIX GPU キャプチャ", nullptr, false, false);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("PIX は現在無効です");
            }
            if (ImGui::MenuItem("PIX を有効化して再起動")) {
                EngineConfig::SetPixRuntimeAndRestart(true);
            }
        }

        ImGui::Separator();

        if (ImGui::MenuItem("終了", "Alt+F4")) {
            PostQuitMessage(0);
        }

        ImGui::EndMenu();
    }

    void GameDebugUI::DrawEditMenu()
    {
        if (!ImGui::BeginMenu("Edit")) {
            return;
        }

        const auto& commandStack = Editor::EditorCommandStack::Get();
        const std::string undoLabel = commandStack.PeekUndoLabel();
        const std::string redoLabel = commandStack.PeekRedoLabel();
        const std::string undoText = undoLabel.empty() ? "元に戻す" : "元に戻す: " + undoLabel;
        const std::string redoText = redoLabel.empty() ? "やり直す" : "やり直す: " + redoLabel;

        if (ImGui::MenuItem(undoText.c_str(), "Ctrl+Z", false,
            sceneDebugEditor_ && sceneDebugEditor_->CanUndo())) {
            sceneDebugEditor_->Undo();
        }
        if (ImGui::MenuItem(redoText.c_str(), "Ctrl+Y", false,
            sceneDebugEditor_ && sceneDebugEditor_->CanRedo())) {
            sceneDebugEditor_->Redo();
        }

        ImGui::Separator();

        if (ImGui::MenuItem("選択を複製", "Ctrl+C", false,
            sceneDebugEditor_ && sceneDebugEditor_->HasSelection())) {
            sceneDebugEditor_->CopySelectedObject();
        }

        ImGui::Separator();

        ImGui::MenuItem("Project Settings…", nullptr, &showProjectSettings_);

        ImGui::EndMenu();
    }

    void GameDebugUI::DrawComponentMenu()
    {
        if (!ImGui::BeginMenu("Component")) {
            return;
        }

        GameObject* const selected = sceneDebugEditor_ ? sceneDebugEditor_->GetSelectedObject() : nullptr;
        if (!selected) {
            ImGui::TextDisabled("オブジェクトを選ぶと足せます");
            ImGui::EndMenu();
            return;
        }

        const ComponentFactory& factory = ComponentFactory::Get();
        for (const std::string& typeName : factory.GetRegisteredTypeNames()) {
            std::string reason;
            const bool canAdd = ComponentEditing::CanAdd(*selected, typeName, &reason);
            const std::string displayName = factory.GetInspectorName(typeName);

            if (ImGui::MenuItem(displayName.empty() ? typeName.c_str() : displayName.c_str(),
                nullptr, false, canAdd)) {
                ComponentEditing::Add(*selected, typeName);
            }
            if (!canAdd && !reason.empty()
                && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("%s", reason.c_str());
            }
        }

        ImGui::EndMenu();
    }

    void GameDebugUI::DrawHelpMenu()
    {
        if (!ImGui::BeginMenu("Help")) {
            return;
        }

        if (Editor::EditorPanel* keyConfig = Editor::EditorPanelRegistry::Get().Find("Key Config")) {
            if (ImGui::MenuItem("キー操作を見る")) {
                keyConfig->visible = true;
            }
        }
        if (ImGui::MenuItem("バージョン情報")) {
            showAboutWindow_ = true;
        }

        ImGui::EndMenu();
    }

    void GameDebugUI::DrawMenuBarChips()
    {
        namespace Theme = Editor::Theme;

        const std::string scene = sceneManager_ ? sceneManager_->GetCurrentSceneName() : std::string{};
        const char* const build = BuildConfigName();

        float width = UI::Bar::ChipWidth(build);
        if (!scene.empty()) {
            width += UI::Bar::ChipWidth(scene.c_str()) + ImGui::GetStyle().ItemSpacing.x;
        }

        const float x = ImGui::GetWindowWidth() - width - 10.0f;
        if (x > ImGui::GetCursorPosX()) {
            ImGui::SetCursorPosX(x);
        }

        if (!scene.empty()) {
            UI::Bar::Chip(scene.c_str(), Theme::kTextDim, Theme::kOutline);
        }
        UI::Bar::Chip(build, Theme::kOk, Theme::WithAlpha(Theme::kOk, 0.35f));
    }

    void GameDebugUI::DrawWindowMenu()
    {
        // すべてのパネルを用途別サブメニューへ振り分ける。
        // パネルが増えても一覧が縦に伸び続けないようにするため、
        // 直下に並べるのは「常に使うもの」だけに絞る。
        {
            if (ImGui::BeginMenu("Window")) {

                auto& registry = Editor::EditorPanelRegistry::Get();

                // Inspector タブを group で絞ってサブメニューへ並べる
                const auto tabMenu = [&registry](Editor::PanelGroup group, const char* label) {
                    const bool hasAny = registry.Any(Editor::PanelPlacement::InspectorTab,
                        [group](const Editor::EditorPanel& p) { return p.desc.group == group; });
                    if (!hasAny || !ImGui::BeginMenu(label)) {
                        return;
                    }
                    registry.ForEach(Editor::PanelPlacement::InspectorTab,
                        [group](Editor::EditorPanel& p) {
                            if (p.desc.group != group) { return; }
                            ImGui::MenuItem(p.Id().c_str(), nullptr, &p.visible);
                        });
                    ImGui::EndMenu();
                    };

                // グループごとの追加項目（そのグループにしか無いもの）
                const auto drawExtra = [&](Editor::PanelGroup group) -> std::function<void()> {
                    switch (group) {
                    case Editor::PanelGroup::General:
                        return [this]() {
                            ImGui::MenuItem("Hierarchy", nullptr, &showHierarchy_);
                            ImGui::MenuItem("Inspector", nullptr, &showInspector_);
                            ImGui::MenuItem("Console", nullptr, &showConsole_);
                            if (ProjectView* const projectView = FindProjectView()) {
                                bool visible = projectView->IsVisible();
                                if (ImGui::MenuItem("Project", nullptr, &visible)) {
                                    projectView->SetVisible(visible);
                                }
                            }
                            };
                    case Editor::PanelGroup::Analysis:
                        return [&tabMenu]() {
                            tabMenu(Editor::PanelGroup::Analysis, "Engine Stats");
                            };
                    case Editor::PanelGroup::Editor:
                        return [this]() {
                            ImGui::MenuItem("Project Settings", nullptr, &showProjectSettings_);
                            };
                    default:
                        return nullptr;
                    }
                    };

                // 並び順は kPanelGroupOrder に一本化する
                for (const auto& [group, groupLabel] : Editor::kPanelGroupOrder) {
                    DrawPanelGroupMenu(group, groupLabel, drawExtra(group));
                }

                // ── アプリ固有のエディタ ──
                tabMenu(Editor::PanelGroup::Application, "Application");

                ImGui::Separator();

                // ── 画面・レイアウト操作 ──
                if (ImGui::BeginMenu("Layout")) {
                    if (auto* winApp = engine_ ? engine_->GetWinApp() : nullptr) {
                        bool fullscreen = winApp->IsFullscreen();
                        if (ImGui::MenuItem("全画面表示", "Alt+Enter", &fullscreen)) {
                            winApp->SetFullscreen(fullscreen);
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("タイトルバーとタスクバーを隠して画面全体に表示します");
                        }
                    }

                    ImGui::MenuItem("エディタUIを隠す", "F11", false, false);

                    ImGui::Separator();

                    ImGui::MenuItem("ゲーム画面のみのウィンドウ", nullptr, &showStandaloneGameWindow_);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "エディタUIを一切含まない独立ウィンドウを開きます。\n"
                            "Release ビルドと同じ見た目を Development のまま確認できます。");
                    }

                    ImGui::Separator();

                    if (ImGui::MenuItem("レイアウトを初期化")) {
                        if (dockingUI_) {
                            dockingUI_->RequestResetLayout();
                        }
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("すべてのパネルを既定位置へ戻します");
                    }
                    ImGui::EndMenu();
                }

                ImGui::EndMenu();
            }
        }
    }

    ProjectView* GameDebugUI::FindProjectView() const
    {
        DebugSubsystem* const debug = engine_ ? engine_->GetDebugSubsystem() : nullptr;
        ImGuiManager* const imGui = debug ? debug->GetImGuiManager() : nullptr;
        return imGui ? imGui->GetProjectView() : nullptr;
    }

    void GameDebugUI::HandleShortcuts()
    {
        // テキスト入力中は文字として扱う
        if (ImGui::GetIO().WantTextInput) {
            return;
        }

        auto& playback = PlaybackStateManager::GetInstance();
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_P, ImGuiInputFlags_RouteGlobal)) {
            if (playback.IsPlaying()) {
                playback.Stop();
            } else {
                playback.Play();
            }
        }
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_P, ImGuiInputFlags_RouteGlobal)) {
            playback.Stop();
        }
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_L, ImGuiInputFlags_RouteGlobal)) {
            if (dockingUI_) {
                dockingUI_->RequestResetLayout();
            }
        }
    }

    void GameDebugUI::RefreshEditorStatus()
    {
        if (!dockingUI_) {
            return;
        }

        EditorStatus status;
        if (auto* frameRate = engine_ ? engine_->GetService<FrameRateController>() : nullptr) {
            status.fps = frameRate->GetCurrentFPS();
        }
        if (sceneDebugEditor_) {
            status.sceneName = sceneDebugEditor_->GetSceneName();
            status.sceneSaved = !sceneDebugEditor_->IsSceneDirty();
        }
        if (sceneManager_ && status.sceneName.empty()) {
            status.sceneName = sceneManager_->GetCurrentSceneName();
        }
        if (auto* script = engine_ ? engine_->GetSubsystem<ScriptSubsystem>() : nullptr) {
            status.scriptOk = script->GetStatus().ok;
            status.scriptTypeCount = script->GetStatus().typeCount;
        }
        status.undoCount = Editor::EditorCommandStack::Get().GetUndoCount();

        // スクリプトのコンパイルが失敗に変わったら、エラーの行が見えるよう Console を前に出す
        if (lastScriptOk_ && !status.scriptOk) {
            showConsole_ = true;
            console_->RequestFocus();
        }
        lastScriptOk_ = status.scriptOk;

        dockingUI_->SetStatus(status);
    }

    void GameDebugUI::DrawAboutWindow()
    {
        if (!showAboutWindow_) {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_FirstUseEver);
        if (auto w = UI::Scope::WindowScope("バージョン情報", &showAboutWindow_,
            ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextColored(Editor::Theme::kAccentHover, "CoreEngine");
            ImGui::Separator();
            ImGui::Text("ビルド構成: %s", BuildConfigName());
            ImGui::Text("Dear ImGui: %s", IMGUI_VERSION);
            if (auto* script = engine_ ? engine_->GetSubsystem<ScriptSubsystem>() : nullptr) {
                ImGui::Text("スクリプトの型: %zu", script->GetStatus().typeCount);
            }
            ImGui::Text("コンポーネントの型: %zu", ComponentFactory::Get().GetRegisteredCount());
        }
    }

    void GameDebugUI::DrawPanelGroupMenu(Editor::PanelGroup group, const char* label,
        const std::function<void()>& extraContent)
    {
        auto& registry = Editor::EditorPanelRegistry::Get();

        // 単独ウィンドウとして開けるものだけを集める
        //（SettingsSection は Project Settings ウィンドウ内なのでここには出さない）
        const auto hasWindow = [&registry, group] {
            return registry.Any(Editor::PanelPlacement::Window,
                [group](const Editor::EditorPanel& p) { return p.desc.group == group; });
            };

        // 中身が何も無いグループはメニュー項目自体を出さない（空のサブメニューを作らない）
        if (!extraContent && !hasWindow()) {
            return;
        }

        if (!ImGui::BeginMenu(label)) {
            return;
        }

        if (extraContent) {
            extraContent();
            if (hasWindow()) {
                ImGui::Separator();
            }
        }

        registry.ForEach(Editor::PanelPlacement::Window, [group](Editor::EditorPanel& panel) {
            if (panel.desc.group != group) { return; }
            ImGui::MenuItem(panel.Id().c_str(), nullptr, &panel.visible);
            });

        ImGui::EndMenu();
    }

    void GameDebugUI::UpdateDebugPanels()
    {
        HandleShortcuts();

        DrawHierarchyPanel();
        DrawInspectorPanel();
        DrawPanelWindows();
        projectSettings_.Draw(showProjectSettings_);
        DrawAboutWindow();

        if (showConsole_) ShowConsoleUI();

        if (dockingUI_) {
            dockingUI_->DrawStatusBar();
        }
    }

    void GameDebugUI::DrawHierarchyPanel()
    {
        if (!showHierarchy_) return;

        if (auto w = UI::Scope::WindowScope("Hierarchy")) {
                if (auto tabBar = UI::Scope::TabBarScope("##HierarchyTabs")) {
                    if (auto tab = UI::Scope::TabItemScope("Objects")) {
                        DrawEnvironmentTree();
                        Editor::EditorPanel* content = Editor::EditorPanelRegistry::Get()
                            .FindFirst(Editor::PanelPlacement::HierarchyContent);
                        if (content && content->desc.draw) {
                            content->desc.draw();
                        } else {
                            UI::Hint("シーンが読み込まれていません");
                        }
                    }
                    if (auto tab = UI::Scope::TabItemScope("Scenes")) {
                        sceneManagerTab_->DrawImGui();
                    }
                }
            }
    }

    void GameDebugUI::DrawEnvironmentTree()
    {
        auto& registry = Editor::EditorPanelRegistry::Get();
        if (!registry.Any(Editor::PanelPlacement::EnvironmentTree, nullptr)) return;

        if (ImGui::TreeNodeEx("Environment",
            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
            registry.ForEach(Editor::PanelPlacement::EnvironmentTree,
                [this](Editor::EditorPanel& entry) {
                const bool selected = (entry.Id() == selectedEnvironmentLabel_);

                // Inspector の表示先を一意にするため、選択時はシーンオブジェクトの選択を解除する
                auto selectThisEntry = [&]() {
                    selectedEnvironmentLabel_ = entry.Id();
                    if (sceneDebugEditor_) {
                        sceneDebugEditor_->ClearSelection();
                    }
                };

                if (entry.desc.childTree) {
                    // 子ツリーを持つエントリ（Lighting の各ライト等）は開閉可能なノードとして描画する
                    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                        | ImGuiTreeNodeFlags_OpenOnDoubleClick
                        | ImGuiTreeNodeFlags_SpanAvailWidth
                        | ImGuiTreeNodeFlags_DefaultOpen;
                    if (selected) flags |= ImGuiTreeNodeFlags_Selected;

                    const bool open = ImGui::TreeNodeEx(entry.Id().c_str(), flags);
                    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                        selectThisEntry();
                        if (entry.desc.onParentSelected) {
                            entry.desc.onParentSelected();
                        }
                    }
                    if (open) {
                        if (entry.desc.childTree()) {
                            selectThisEntry();
                        }
                        ImGui::TreePop();
                    }
                } else {
                    if (ImGui::Selectable(entry.Id().c_str(), selected)) {
                        selectThisEntry();
                    }
                }
                });
            ImGui::TreePop();
        }
        ImGui::Separator();
    }

    Editor::EditorPanel* GameDebugUI::FindSelectedEnvironmentEntry()
    {
        if (selectedEnvironmentLabel_.empty()) return nullptr;
        Editor::EditorPanel* found =
            Editor::EditorPanelRegistry::Get().Find(selectedEnvironmentLabel_);
        if (found && found->desc.placement == Editor::PanelPlacement::EnvironmentTree) {
            return found;
        }
        // 登録が外れていたら選択も落とす（Inspector が消えた中身を指し続けないように）
        selectedEnvironmentLabel_.clear();
        return nullptr;
    }

    void GameDebugUI::DrawInspectorPanel()
    {
        if (!showInspector_) return;

        if (auto w = UI::Scope::WindowScope("Inspector")) {
            if (auto tabBar = UI::Scope::TabBarScope("##InspectorTabs", ImGuiTabBarFlags_AutoSelectNewTabs)) {
                // Object タブ（常時表示、閉じるボタンなし）
                // 環境エディタ選択中はその内容を、シーンオブジェクト選択中はそのプロパティを表示する
                if (auto tab = UI::Scope::TabItemScope("Object")) {
                    // シーンオブジェクトが選択されたら環境エディタの選択は解除（後から選んだ方を優先）
                    if (sceneDebugEditor_ && sceneDebugEditor_->HasSelection()) {
                        selectedEnvironmentLabel_.clear();
                    }

                    Editor::EditorPanel* env = FindSelectedEnvironmentEntry();
                    Editor::EditorPanel* object = Editor::EditorPanelRegistry::Get()
                        .FindFirst(Editor::PanelPlacement::InspectorObject);
                    ProjectView* const projectView = FindProjectView();
                    const bool hasObject = sceneDebugEditor_ && sceneDebugEditor_->HasSelection();

                    if (env && env->desc.draw) {
                        ImGui::SeparatorText(env->Id().c_str());
                        env->desc.draw();
                    } else if (!hasObject && projectView && !projectView->GetSelectedAsset().empty()) {
                        // シーンのオブジェクトを選んでいないときは、Project で選んだアセットを出す
                        projectView->DrawSelectedAssetInspector();
                    } else if (object && object->desc.draw) {
                        object->desc.draw();
                    } else {
                        UI::Hint("シーンが読み込まれていません");
                    }
                }

                // 登録されたタブ（×ボタンで閉じられる）
                Editor::EditorPanelRegistry::Get().ForEach(Editor::PanelPlacement::InspectorTab,
                    [](Editor::EditorPanel& entry) {
                        if (!entry.visible) return;
                        if (auto tab = UI::Scope::TabItemScope(entry.Id().c_str(), &entry.visible)) {
                            if (entry.desc.draw) entry.desc.draw();
                        }
                    });
            }
        }
    }

    void GameDebugUI::ShowConsoleUI()
    {
        console_->SetVisible(showConsole_);
        console_->Draw();
        // exit コマンドで閉じたときはメニューの表示もそろえる
        showConsole_ = console_->IsVisible();
    }

    void GameDebugUI::DrawPanelWindows()
    {
        Editor::EditorPanelRegistry::Get().ForEach(Editor::PanelPlacement::Window,
            [](Editor::EditorPanel& panel) {
                if (!panel.visible) return;
                // フローティングで開くので初回サイズを与える
                ImGui::SetNextWindowSize(
                    ImVec2(panel.desc.defaultWidth, panel.desc.defaultHeight),
                    ImGuiCond_FirstUseEver);
                if (ImGui::Begin(panel.Id().c_str(), &panel.visible)) {
                    if (panel.desc.draw) panel.desc.draw();
                }
                ImGui::End();
            });
    }

    void GameDebugUI::RegisterWindowsForDocking()
    {
        if (!dockingUI_) return;

        // 常設パネルの既定位置（モック① の標準レイアウト）
        dockingUI_->RegisterWindow("Hierarchy", Editor::DockArea::LeftTop);
        dockingUI_->RegisterWindow("Project", Editor::DockArea::LeftBottom);
        dockingUI_->RegisterWindow("Inspector", Editor::DockArea::Right);
        dockingUI_->RegisterWindow(consoleWindow, Editor::DockArea::Bottom);
    }
}
#endif // USE_IMGUI
