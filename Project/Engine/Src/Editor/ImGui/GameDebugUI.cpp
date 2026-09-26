#include "pch.h"
#include "GameDebugUI.h"

#ifdef CORE_EDITOR
#include "Editor/Command/EditorCommandStack.h"
#include "Editor/ImGui/DockingUI.h"
#include "Editor/ImGui/EditorTheme.h"
#include "Editor/ImGui/ImGuiManager.h"
#include "Editor/ImGui/ProjectView.h"
#include "Editor/ImGui/Widgets/EditorBars.h"
#include "Editor/Inspector/ComponentInspectors.h"
#include "Editor/Panel/EditorPanelRegistry.h"
#include "Editor/Scene/ComponentEditing.h"
#include "Editor/Scene/PlayModeController.h"
#include "Editor/Scene/SceneDebugEditor.h"
#include "EngineSystem/EngineSystem.h"
#include "EngineSystem/EngineConfig.h"
#include "EngineSystem/PlaybackState.h"
#include "EngineSystem/Relaunch.h"
#include "EngineSystem/Settings/ProjectSettings.h"
#include "GameObject/Component/Core/ComponentFactory.h"
#include "Scene/SceneManager.h"
#include "Scene/SceneSaveSystem.h"
#include "Script/ScriptHost.h"
#include "Script/ScriptSubsystem.h"
#include "Utility/FrameRate/FrameRateController.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/Path/ProjectPaths.h"
#include "WinApp/WinApp.h"
#include <imgui.h>
#include <algorithm>


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
        DrawGameObjectMenu();
        DrawComponentMenu();
        DrawAssetsMenu();
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

        // 再生中はプロジェクトもシーンも開かせない
        const bool editing = PlaybackStateManager::GetInstance().IsEditing();
        constexpr const char* kStopFirst = "再生中は開けません。停止してから開いてください";

        // プロジェクト（切り替えるときはエディタを起動し直す）
        if (ImGui::MenuItem("新しいプロジェクト…", nullptr, false, editing)) {
            OpenProjectBrowser(true);
        }
        if (!editing && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", kStopFirst);
        }
        if (ImGui::MenuItem("プロジェクトを開く…", nullptr, false, editing)) {
            OpenProjectBrowser(false);
        }
        if (!editing && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", kStopFirst);
        }
        if (ImGui::BeginMenu("最近のプロジェクト", editing)) {
            DrawRecentProjectsMenu();
            ImGui::EndMenu();
        }
        if (!editing && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", kStopFirst);
        }

        ImGui::Separator();

        const bool hasScene = sceneDebugEditor_ && !sceneDebugEditor_->GetSceneName().empty();
        if (ImGui::MenuItem("シーンを保存", "Ctrl+S", false, hasScene)) {
            sceneDebugEditor_->SaveScene();
        }

        if (ImGui::MenuItem("新しいシーン…", nullptr, false, editing && sceneManager_ != nullptr)) {
            newSceneName_[0] = '\0';
            newSceneError_.clear();
            showNewSceneDialog_ = true;
        }
        if (sceneManager_ && !editing && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", kStopFirst);
        }

        if (sceneManager_ && ImGui::BeginMenu("シーンを開く", editing)) {
            const std::string current = sceneManager_->GetCurrentSceneName();
            for (const std::string& name : sceneManager_->GetAllSceneNames()) {
                if (ImGui::MenuItem(name.c_str(), nullptr, name == current)) {
                    RequestOpenScene(name);
                }
            }
            ImGui::EndMenu();
        }
        if (sceneManager_ && !editing && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", kStopFirst);
        }

        const std::string currentScene = sceneManager_ ? sceneManager_->GetCurrentSceneName() : std::string{};
        const bool canReload = sceneManager_ && sceneManager_->HasScene(currentScene);
        if (ImGui::MenuItem("シーンを再読み込み", nullptr, false, canReload && editing)) {
            sceneManager_->ChangeScene(currentScene);
        }
        if (!editing && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", kStopFirst);
        }

        ImGui::Separator();

        if (ImGui::MenuItem("ゲームを書き出す…")) {
            gameExportDialog_.Open(sceneDebugEditor_ && sceneDebugEditor_->IsSceneDirty());
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

        ImGui::MenuItem("Project Settings…", nullptr, &showProjectSettings_);

        ImGui::EndMenu();
    }

    void GameDebugUI::DrawGameObjectMenu()
    {
        if (!ImGui::BeginMenu("GameObject")) {
            return;
        }

        if (ImGui::MenuItem("空のオブジェクトを作成", nullptr, false, sceneDebugEditor_ != nullptr)) {
            sceneDebugEditor_->CreateEmptyObject();
        }
        if (ImGui::BeginMenu("エフェクト", sceneDebugEditor_ != nullptr)) {
            if (ImGui::MenuItem("パーティクル")) {
                sceneDebugEditor_->CreateParticleObject(ObjectEditing::ParticleKind::Cpu);
            }
            if (ImGui::MenuItem("GPU パーティクル")) {
                sceneDebugEditor_->CreateParticleObject(ObjectEditing::ParticleKind::Gpu);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("UI", sceneDebugEditor_ != nullptr)) {
            if (ImGui::MenuItem("テキスト")) {
                sceneDebugEditor_->CreateUIObject(ObjectEditing::UIElementKind::Text);
            }
            if (ImGui::MenuItem("画像")) {
                sceneDebugEditor_->CreateUIObject(ObjectEditing::UIElementKind::Image);
            }
            ImGui::EndMenu();
        }

        ImGui::Separator();

        // 押せないときは理由をツールチップに出す
        std::string reason;
        const bool canEdit = sceneDebugEditor_ && sceneDebugEditor_->CanEditSelectedObject(&reason);
        const auto showReason = [&canEdit, &reason]() {
            if (!canEdit && !reason.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("%s", reason.c_str());
            }
        };
        if (ImGui::MenuItem("複製", "Ctrl+D", false, canEdit)) {
            sceneDebugEditor_->DuplicateSelectedObject();
        }
        showReason();
        if (ImGui::MenuItem("削除", "Del", false, canEdit)) {
            sceneDebugEditor_->DeleteSelectedObject();
        }
        showReason();

        ImGui::EndMenu();
    }

    void GameDebugUI::DrawAssetsMenu()
    {
        if (!ImGui::BeginMenu("Assets")) {
            return;
        }

        ScriptSubsystem* const script = engine_ ? engine_->GetSubsystem<ScriptSubsystem>() : nullptr;
        if (ImGui::MenuItem("スクリプトを再読み込み", "Ctrl+R", false, script != nullptr)) {
            script->RequestReload();
        }

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
            const std::string displayName = Editor::ComponentInspectors::DisplayNameOf(typeName);

            if (ImGui::MenuItem(displayName.c_str(), nullptr, false, canAdd)) {
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

        const std::string project = ProjectSettings::Get().GetProjectName();
        const std::string scene = sceneManager_ ? sceneManager_->GetCurrentSceneName() : std::string{};
        const char* const build = BuildConfigName();
        const bool inPlayMode = PlaybackStateManager::GetInstance().IsInPlayMode();
        constexpr const char* kPlayMode = "Play Mode";
        const float spacing = ImGui::GetStyle().ItemSpacing.x;

        float width = UI::Bar::ChipWidth(build) + UI::Bar::ChipWidth(project.c_str()) + spacing;
        if (!scene.empty()) {
            width += UI::Bar::ChipWidth(scene.c_str()) + spacing;
        }
        if (inPlayMode) {
            width += UI::Bar::ChipWidth(kPlayMode) + spacing;
        }

        const float x = ImGui::GetWindowWidth() - width - 10.0f;
        if (x > ImGui::GetCursorPosX()) {
            ImGui::SetCursorPosX(x);
        }

        if (inPlayMode) {
            UI::Bar::Chip(kPlayMode, Theme::kWarm, Theme::WithAlpha(Theme::kWarm, 0.6f));
        }
        UI::Bar::Chip(project.c_str(), Theme::kAccentHover, Theme::WithAlpha(Theme::kAccentHover, 0.45f));
        if (ImGui::IsItemHovered()) {
            const std::u8string folder = ProjectPaths::ProjectRoot().u8string();
            ImGui::SetTooltip("開いているプロジェクト\n%s", std::string(folder.begin(), folder.end()).c_str());
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

        // 再生（停止）・一時停止・コマ送り
        auto& playback = PlaybackStateManager::GetInstance();
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_P, ImGuiInputFlags_RouteGlobal)) {
            if (playback.IsInPlayMode()) {
                playback.Stop();
            } else {
                playback.Play();
            }
        }
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_P, ImGuiInputFlags_RouteGlobal)) {
            playback.TogglePause();
        }
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiKey_P, ImGuiInputFlags_RouteGlobal)) {
            playback.RequestStep();
        }
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_L, ImGuiInputFlags_RouteGlobal)) {
            if (dockingUI_) {
                dockingUI_->RequestResetLayout();
            }
        }
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_R, ImGuiInputFlags_RouteGlobal)) {
            if (auto* script = engine_ ? engine_->GetSubsystem<ScriptSubsystem>() : nullptr) {
                script->RequestReload();
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
            if (const ScriptHost* const host = script->GetHost()) {
                status.scriptUpdateMs = host->GetFrameStats().updateMs;
                status.scriptComponents = host->GetFrameStats().liveComponents;
            }
        }
        status.undoCount = Editor::EditorCommandStack::Get().GetUndoCount();

        const auto& playback = PlaybackStateManager::GetInstance();
        status.playback = playback.GetState();
        if (playModeController_) {
            status.snapshotObjects = playModeController_->GetSnapshotObjectCount();
            status.snapshotSeconds = playModeController_->GetCaptureSeconds();
            status.playTime = playModeController_->GetPlayTime();
        }

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

    void GameDebugUI::DrawNewSceneDialog()
    {
        constexpr const char* kTitle = "新しいシーン";
        if (showNewSceneDialog_ && !ImGui::IsPopupOpen(kTitle)) {
            ImGui::OpenPopup(kTitle);
        }

        ImGui::SetNextWindowSize(ImVec2(380.0f, 0.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }

        UI::Hint("Application/Assets/Scenes の下にフォルダを作り、その場で開きます。");
        ImGui::Separator();

        UI::InputText("名前", newSceneName_, sizeof(newSceneName_));
        ImGui::RadioButton("空", &newSceneTemplate_, 0);
        UI::SameLine();
        ImGui::RadioButton("基本（太陽と床）", &newSceneTemplate_, 1);

        if (!newSceneError_.empty()) {
            ImGui::TextColored(Editor::Theme::kError, "%s", newSceneError_.c_str());
        }

        ImGui::Separator();
        if (ImGui::Button("作成")) {
            std::string error;
            if (SceneSaveSystem::IsValidSceneName(newSceneName_, &error)) {
                newSceneError_.clear();
                showNewSceneDialog_ = false;
                ImGui::CloseCurrentPopup();
                RequestCreateScene(newSceneName_, newSceneTemplate_);
            } else {
                newSceneError_ = error;
            }
        }
        UI::SameLine();
        if (ImGui::Button("やめる")) {
            showNewSceneDialog_ = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    void GameDebugUI::DrawUnsavedChangesDialog()
    {
        constexpr const char* kTitle = "保存していない変更があります";
        const bool waiting = pendingSceneAction_ != PendingSceneAction::None;
        if (waiting && !ImGui::IsPopupOpen(kTitle)) {
            ImGui::OpenPopup(kTitle);
        }

        if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }

        UI::Hint("このシーンの変更はまだ保存されていません。");
        ImGui::Separator();

        if (ImGui::Button("保存して続ける")) {
            if (sceneDebugEditor_) {
                sceneDebugEditor_->SaveScene();
            }
            ImGui::CloseCurrentPopup();
            RunPendingSceneAction();
        }
        UI::SameLine();
        if (ImGui::Button("保存せずに続ける")) {
            ImGui::CloseCurrentPopup();
            RunPendingSceneAction();
        }
        UI::SameLine();
        if (ImGui::Button("やめる")) {
            pendingSceneAction_ = PendingSceneAction::None;
            pendingSceneName_.clear();
            pendingProjectFolder_.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    void GameDebugUI::RequestOpenScene(const std::string& name)
    {
        if (!sceneManager_) {
            return;
        }
        if (sceneDebugEditor_ && sceneDebugEditor_->IsSceneDirty()) {
            pendingSceneAction_ = PendingSceneAction::Open;
            pendingSceneName_ = name;
            return;
        }
        sceneManager_->ChangeScene(name);
    }

    void GameDebugUI::RequestCreateScene(const std::string& name, int templateIndex)
    {
        if (!sceneManager_) {
            return;
        }
        if (sceneDebugEditor_ && sceneDebugEditor_->IsSceneDirty()) {
            pendingSceneAction_ = PendingSceneAction::Create;
            pendingSceneName_ = name;
            pendingSceneTemplate_ = templateIndex;
            return;
        }
        CreateAndOpenScene(name, templateIndex);
    }

    void GameDebugUI::RunPendingSceneAction()
    {
        const PendingSceneAction action = pendingSceneAction_;
        const std::string name = pendingSceneName_;
        const std::filesystem::path projectFolder = pendingProjectFolder_;
        pendingSceneAction_ = PendingSceneAction::None;
        pendingSceneName_.clear();
        pendingProjectFolder_.clear();

        switch (action) {
        case PendingSceneAction::Open:
            if (sceneManager_) {
                sceneManager_->ChangeScene(name);
            }
            break;
        case PendingSceneAction::Create:
            CreateAndOpenScene(name, pendingSceneTemplate_);
            break;
        case PendingSceneAction::SwitchProject:
            Relaunch::RequestProject(projectFolder);
            break;
        default:
            break;
        }
    }

    bool GameDebugUI::CreateAndOpenScene(const std::string& name, int templateIndex)
    {
        if (!sceneManager_) {
            return false;
        }

        const auto kind = (templateIndex == 0)
            ? SceneSaveSystem::SceneTemplate::Empty
            : SceneSaveSystem::SceneTemplate::Basic;

        std::string error;
        if (!SceneSaveSystem::CreateScene(name, kind, &error)) {
            newSceneError_ = error;
            showNewSceneDialog_ = true;   // 窓を開き直して理由を見せる
            return false;
        }

        // 作ったシーンはその場で開けるようにする（再起動を待たせない）
        sceneManager_->RegisterDataScene(name);
        sceneManager_->ChangeScene(name);
        return true;
    }

    void GameDebugUI::OpenProjectBrowser(bool newProject)
    {
        WinApp* const winApp = engine_ ? engine_->GetWinApp() : nullptr;
        projectList_.Load();
        projectBrowser_ = std::make_unique<Editor::ProjectBrowser>(
            projectList_, winApp ? winApp->GetHwnd() : nullptr, ProjectPaths::ProjectRoot());
        if (newProject) {
            projectBrowser_->ShowNewProject();
        }
    }

    void GameDebugUI::DrawProjectBrowser()
    {
        constexpr const char* kTitle = "プロジェクト";
        if (!projectBrowser_) {
            return;
        }
        if (!ImGui::IsPopupOpen(kTitle)) {
            ImGui::OpenPopup(kTitle);
        }

        // 画面の真ん中に、ランチャーと同じ大きさと地の色で出す
        const float fontSize = ImGui::GetFontSize();
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 size(std::min(viewport->WorkSize.x - fontSize * 2.0f, fontSize * 92.0f),
                          std::min(viewport->WorkSize.y - fontSize * 2.0f, fontSize * 56.0f));
        ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(size, ImGuiCond_Appearing);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, Editor::Theme::kWindow);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        bool open = true;
        const bool visible = ImGui::BeginPopupModal(kTitle, &open,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        if (!visible) {
            // × で閉じたときだけ中身を捨てる
            if (!open) {
                projectBrowser_.reset();
            }
            return;
        }

        projectBrowser_->Draw();

        // 開くプロジェクトが決まるか、Esc で閉じる
        const std::filesystem::path chosen = projectBrowser_->TakeChosen();
        const bool escape = !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false);
        const bool close = !chosen.empty() || escape;
        if (close) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();

        if (close) {
            projectBrowser_.reset();
            if (!chosen.empty()) {
                RequestSwitchProject(chosen);
            }
        }
    }

    void GameDebugUI::DrawRecentProjectsMenu()
    {
        // サブメニューが開いたときに一覧を読み直す
        if (ImGui::IsWindowAppearing()) {
            projectList_.Load();
            recentProjects_ = projectList_.Collect();
        }

        // 名前と場所を、最近開いた順に 10 件まで並べる
        constexpr size_t kMaxItems = 10;
        const std::filesystem::path current = ProjectPaths::ProjectRoot();
        for (size_t i = 0; i < recentProjects_.size() && i < kMaxItems; ++i) {
            const Editor::ProjectEntry& entry = recentProjects_[i];
            const bool isCurrent = Editor::ProjectList::IsSameFolder(entry.folder, current);
            const std::u8string folderText = entry.folder.u8string();
            const std::string folder(folderText.begin(), folderText.end());

            ImGui::PushID(static_cast<int>(i));
            if (ImGui::MenuItem(entry.name.c_str(), folder.c_str(), isCurrent, !isCurrent && !entry.missing)) {
                RequestSwitchProject(entry.folder);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                if (isCurrent) {
                    ImGui::SetTooltip("開いているプロジェクトです");
                } else if (entry.missing) {
                    ImGui::SetTooltip("フォルダが見つかりません");
                }
            }
            ImGui::PopID();
        }
        if (recentProjects_.empty()) {
            ImGui::TextDisabled("まだありません");
        }
    }

    void GameDebugUI::RequestSwitchProject(const std::filesystem::path& folder)
    {
        if (Editor::ProjectList::IsSameFolder(folder, ProjectPaths::ProjectRoot())) {
            return;
        }
        if (sceneDebugEditor_ && sceneDebugEditor_->IsSceneDirty()) {
            pendingSceneAction_ = PendingSceneAction::SwitchProject;
            pendingProjectFolder_ = folder;
            return;
        }
        Relaunch::RequestProject(folder);
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
        DrawNewSceneDialog();
        DrawProjectBrowser();
        gameExportDialog_.Draw();
        DrawUnsavedChangesDialog();

        if (showConsole_) ShowConsoleUI();

        if (dockingUI_) {
            dockingUI_->DrawStatusBar();
        }
    }

    void GameDebugUI::DrawHierarchyPanel()
    {
        if (!showHierarchy_) return;

        if (auto w = UI::Scope::WindowScope("Hierarchy")) {
            if (sceneDebugEditor_) {
                sceneDebugEditor_->HandleSelectionShortcuts();
            }
            Editor::EditorPanel* content = Editor::EditorPanelRegistry::Get()
                .FindFirst(Editor::PanelPlacement::HierarchyContent);
            if (content && content->desc.draw) {
                content->desc.draw();
            } else {
                UI::Hint("シーンが読み込まれていません");
            }
        }
    }

    void GameDebugUI::DrawInspectorPanel()
    {
        if (!showInspector_) return;

        if (auto w = UI::Scope::WindowScope("Inspector")) {
            if (auto tabBar = UI::Scope::TabBarScope("##InspectorTabs", ImGuiTabBarFlags_AutoSelectNewTabs)) {
                // Object タブ（常時表示、閉じるボタンなし）
                // シーンのオブジェクトを選んでいればそのプロパティを表示する
                if (auto tab = UI::Scope::TabItemScope("Object")) {
                    Editor::EditorPanel* object = Editor::EditorPanelRegistry::Get()
                        .FindFirst(Editor::PanelPlacement::InspectorObject);
                    ProjectView* const projectView = FindProjectView();
                    const bool hasObject = sceneDebugEditor_ && sceneDebugEditor_->HasSelection();

                    if (!hasObject && projectView && !projectView->GetSelectedAsset().empty()) {
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
#endif // CORE_EDITOR
