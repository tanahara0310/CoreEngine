#include "pch.h"
#include "GameDebugUI.h"

#ifdef USE_IMGUI
#include "Editor/ImGui/DockingUI.h"
#include "Editor/Scene/SceneDebugEditor.h"
#include "EngineSystem/EngineSystem.h"
#include "EngineSystem/EngineConfig.h"
#include "Utility/FrameRate/FrameRateController.h"
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <iterator>


namespace CoreEngine
{
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

        console_->LogInfo("GameDebugUIが正常に初期化されました");
        console_->LogDebug("エンジンシステムが正常に接続されました");
    }

    void GameDebugUI::SetSceneManager(SceneManager* sceneManager)
    {
        if (sceneManager) {
            sceneManagerTab_->Initialize(sceneManager);
            console_->LogInfo("SceneManagerがSceneManagerTabに設定されました");
        }
    }

    void GameDebugUI::SetInspectorCameraDrawer(std::function<void()> callback)
    {
        // 既存の "Camera Editor" パネルがあればコールバックを更新するだけにする
        // （シーン切り替え時に重複登録されないようにする）
        for (auto& p : enginePanels_) {
            if (p.label == "Camera Editor") {
                p.drawer = std::move(callback);
                return;
            }
        }
        // Camera Editorを作業用ツールとして登録（Toolsメニューから開くフローティングウィンドウ）
        RegisterEnginePanel("Camera Editor", std::move(callback), EnginePanelCategory::Tools);
    }

    void GameDebugUI::RegisterAppEditor(const std::string& label, std::function<void()> drawer)
    {
        for (auto& entry : appEditors_) {
            if (entry.label == label) { entry.drawer = std::move(drawer); return; }
        }
        appEditors_.push_back({ label, std::move(drawer), false });
    }

    void GameDebugUI::RegisterEngineEditor(const std::string& label, std::function<void()> drawer)
    {
        for (auto& entry : engineEditors_) {
            if (entry.label == label) { entry.drawer = std::move(drawer); return; }
        }
        engineEditors_.push_back({ label, std::move(drawer), false });
    }

    void GameDebugUI::RegisterEnginePanel(const std::string& label, std::function<void()> drawer,
        EnginePanelCategory category)
    {
        for (auto& p : enginePanels_) {
            if (p.label == label) { p.drawer = std::move(drawer); p.category = category; return; }
        }
        enginePanels_.push_back({ label, std::move(drawer), false, category });
        // Settings は Engine Settings ウィンドウ内のセクション、Tools はフローティングで開くため
        // どちらもドッキングシステムへは登録しない
    }

    void GameDebugUI::RegisterEnvironmentEditor(const std::string& label, const void* owner,
        std::function<void()> drawer, std::function<bool()> childTree,
        std::function<void()> onParentSelected)
    {
        for (auto& e : environmentEditors_) {
            if (e.label == label) {
                e.owner = owner;
                e.drawer = std::move(drawer);
                e.childTree = std::move(childTree);
                e.onParentSelected = std::move(onParentSelected);
                return;
            }
        }
        environmentEditors_.push_back(
            { label, owner, std::move(drawer), std::move(childTree), std::move(onParentSelected) });
    }

    void GameDebugUI::UnregisterEnvironmentEditor(const std::string& label, const void* owner)
    {
        // owner が一致する場合のみ解除する。
        // シーン切り替えで「新シーンの登録 → 旧シーンの破棄」の順になっても
        // 新シーンの登録を旧シーンのデストラクタが消してしまわないようにするため。
        std::erase_if(environmentEditors_, [&](const EnvironmentEntry& e) {
            return e.label == label && e.owner == owner;
        });
        if (selectedEnvironmentLabel_ == label && !FindSelectedEnvironmentEntry()) {
            selectedEnvironmentLabel_.clear();
        }
    }

    void GameDebugUI::RegisterEngineDebugPanel(const std::string& label, std::function<void()> drawer)
    {
        for (auto& p : engineDebugPanels_) {
            if (p.label == label) { p.drawer = std::move(drawer); return; }
        }
        engineDebugPanels_.push_back({ label, std::move(drawer), false });
    }

    void GameDebugUI::SetPanelVisible(const std::string& label, bool visible)
    {
        for (auto& p : enginePanels_) {
            if (p.label == label) { p.visible = visible; return; }
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

        if (ImGui::BeginMainMenuBar()) {
            // Window メニュー：常設パネルの表示トグルと Engine Settings
            if (ImGui::BeginMenu("Window")) {
                ImGui::MenuItem("Hierarchy", nullptr, &showHierarchy_);
                ImGui::MenuItem("Inspector", nullptr, &showInspector_);
                ImGui::MenuItem("Console", nullptr, &showConsole_);

                ImGui::Separator();
                ImGui::MenuItem("Engine Settings", nullptr, &showEngineSettings_);

                if (!appEditors_.empty()) {
                    ImGui::Separator();
                    for (auto& entry : appEditors_) {
                        ImGui::MenuItem(entry.label.c_str(), nullptr, &entry.visible);
                    }
                }
                ImGui::EndMenu();
            }

            // Tools メニュー：作業用ツールウィンドウ（フローティング）
            if (ImGui::BeginMenu("Tools")) {
                for (auto& p : enginePanels_) {
                    if (p.category != EnginePanelCategory::Tools) continue;
                    ImGui::MenuItem(p.label.c_str(), nullptr, &p.visible);
                }
                ImGui::EndMenu();
            }

            // Debug メニュー：エンジン統計（Inspectorタブ）とデバッグパネル
            if (ImGui::BeginMenu("Debug")) {
                if (ImGui::BeginMenu("Engine Stats")) {
                    for (auto& entry : engineEditors_) {
                        ImGui::MenuItem(entry.label.c_str(), nullptr, &entry.visible);
                    }
                    ImGui::EndMenu();
                }
                for (auto& p : engineDebugPanels_) {
                    ImGui::MenuItem(p.label.c_str(), nullptr, &p.visible);
                }
                ImGui::EndMenu();
            }

            // Capture メニュー（右端に配置）
            float captureMenuWidth = ImGui::CalcTextSize("Capture").x + ImGui::GetStyle().ItemSpacing.x * 4.0f;
            ImGui::SameLine(ImGui::GetWindowWidth() - captureMenuWidth);
            if (ImGui::BeginMenu("Capture")) {
                if (ImGui::MenuItem("Screenshot")) {
                    screenCapture_.RequestCapture();
                }

                ImGui::Separator();

                if (PixCapture::IsPixAvailable()) {
                    if (ImGui::MenuItem("PIX GPU Capture")) {
                        pixCapture_.RequestCapture();
                    }
                    if (ImGui::MenuItem("PIX を無効化して再起動")) {
                        EngineConfig::SetPixRuntimeAndRestart(false);
                    }
                } else {
                    ImGui::BeginDisabled();
                    ImGui::MenuItem("PIX GPU Capture");
                    ImGui::EndDisabled();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                        ImGui::SetTooltip("PIX は現在無効です");
                    }
                    if (ImGui::MenuItem("PIX を有効化して再起動")) {
                        EngineConfig::SetPixRuntimeAndRestart(true);
                    }
                }

                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }
    }

    void GameDebugUI::UpdateDebugPanels()
    {
        DrawHierarchyPanel();
        DrawInspectorPanel();
        DrawEnginePanels();
        DrawEngineDebugPanels();
        DrawEngineSettingsWindow();

        if (showConsole_) ShowConsoleUI();

        if (dockingUI_) {
            float fps = 0.0f;
            float deltaTimeMs = 0.0f;
            if (auto* frameRate = engine_->GetComponent<FrameRateController>()) {
                fps = frameRate->GetCurrentFPS();
                deltaTimeMs = frameRate->GetDeltaTime() * 1000.0f;
            }
            dockingUI_->DrawStatusBar(fps, deltaTimeMs);
        }
    }

    void GameDebugUI::DrawHierarchyPanel()
    {
        if (!showHierarchy_) return;

        if (auto w = UI::Scope::WindowScope("Hierarchy")) {
                if (auto tabBar = UI::Scope::TabBarScope("##HierarchyTabs")) {
                    if (auto tab = UI::Scope::TabItemScope("Objects")) {
                        DrawEnvironmentTree();
                        if (hierarchyContentDrawer_) {
                            hierarchyContentDrawer_();
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
        if (environmentEditors_.empty()) return;

        if (ImGui::TreeNodeEx("Environment",
            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
            for (const auto& entry : environmentEditors_) {
                const bool selected = (entry.label == selectedEnvironmentLabel_);

                // Inspector の表示先を一意にするため、選択時はシーンオブジェクトの選択を解除する
                auto selectThisEntry = [&]() {
                    selectedEnvironmentLabel_ = entry.label;
                    if (sceneDebugEditor_) {
                        sceneDebugEditor_->ClearSelection();
                    }
                };

                if (entry.childTree) {
                    // 子ツリーを持つエントリ（Lighting の各ライト等）は開閉可能なノードとして描画する
                    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                        | ImGuiTreeNodeFlags_OpenOnDoubleClick
                        | ImGuiTreeNodeFlags_SpanAvailWidth
                        | ImGuiTreeNodeFlags_DefaultOpen;
                    if (selected) flags |= ImGuiTreeNodeFlags_Selected;

                    const bool open = ImGui::TreeNodeEx(entry.label.c_str(), flags);
                    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                        selectThisEntry();
                        if (entry.onParentSelected) {
                            entry.onParentSelected();
                        }
                    }
                    if (open) {
                        if (entry.childTree()) {
                            selectThisEntry();
                        }
                        ImGui::TreePop();
                    }
                } else {
                    if (ImGui::Selectable(entry.label.c_str(), selected)) {
                        selectThisEntry();
                    }
                }
            }
            ImGui::TreePop();
        }
        ImGui::Separator();
    }

    const GameDebugUI::EnvironmentEntry* GameDebugUI::FindSelectedEnvironmentEntry() const
    {
        if (selectedEnvironmentLabel_.empty()) return nullptr;
        for (const auto& entry : environmentEditors_) {
            if (entry.label == selectedEnvironmentLabel_) return &entry;
        }
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

                    if (const EnvironmentEntry* env = FindSelectedEnvironmentEntry(); env && env->drawer) {
                        ImGui::SeparatorText(env->label.c_str());
                        env->drawer();
                    } else if (inspectorObjectDrawer_) {
                        inspectorObjectDrawer_();
                    } else {
                        UI::Hint("シーンが読み込まれていません");
                    }
                }

                // エンジンエディタータブ（×ボタンでタブを閉じられる）
                for (auto& entry : engineEditors_) {
                    if (!entry.visible) continue;
                    if (auto tab = UI::Scope::TabItemScope(entry.label.c_str(), &entry.visible)) {
                        if (entry.drawer) entry.drawer();
                    }
                }

                // アプリエディタータブ（×ボタンでタブを閉じられる）
                for (auto& entry : appEditors_) {
                    if (!entry.visible) continue;
                    if (auto tab = UI::Scope::TabItemScope(entry.label.c_str(), &entry.visible)) {
                        if (entry.drawer) entry.drawer();
                    }
                }
            }
        }
    }

    void GameDebugUI::ShowConsoleUI()
    {
        console_->SetVisible(showConsole_);
        console_->Draw();
    }

    void GameDebugUI::DrawEnginePanels()
    {
        // 独立ウィンドウとして描画するのは Tools カテゴリのみ
        //（Settings カテゴリは Engine Settings ウィンドウ内のセクションとして描画される）
        for (auto& panel : enginePanels_) {
            if (panel.category != EnginePanelCategory::Tools) continue;
            if (!panel.visible) continue;
            // ツールはドックに登録されずフローティングで開くため、初回サイズを与える
            ImGui::SetNextWindowSize(ImVec2(460.0f, 540.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin(panel.label.c_str(), &panel.visible)) {
                if (panel.drawer) panel.drawer();
            }
            ImGui::End();
        }
    }

    void GameDebugUI::DrawEngineDebugPanels()
    {
        for (auto& panel : engineDebugPanels_) {
            if (!panel.visible) continue;
            // デバッグ情報パネルは表形式が多く 420px では列が潰れるため、初回サイズを広めに取る
            ImGui::SetNextWindowSize(ImVec2(620.0f, 620.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin(panel.label.c_str(), &panel.visible)) {
                if (panel.drawer) panel.drawer();
            }
            ImGui::End();
        }
    }

    void GameDebugUI::DrawEngineSettingsWindow()
    {
        if (!showEngineSettings_) return;

        ImGui::SetNextWindowSize(ImVec2(780.0f, 560.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(480.0f, 280.0f), ImVec2(1600.0f, 1200.0f));
        if (!ImGui::Begin("Engine Settings", &showEngineSettings_)) {
            ImGui::End();
            return;
        }

        // ── 検索ボックス ──
        ImGui::SetNextItemWidth(220.0f);
        ImGui::InputTextWithHint("##settings_filter", "検索...", settingsFilter_, sizeof(settingsFilter_));
        ImGui::Separator();

        // 大文字小文字を無視した部分一致
        auto matchesFilter = [this](const std::string& label) {
            if (settingsFilter_[0] == '\0') return true;
            std::string target = label;
            std::string query = settingsFilter_;
            std::transform(target.begin(), target.end(), target.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            std::transform(query.begin(), query.end(), query.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return target.find(query) != std::string::npos;
        };

        const float leftPaneW = 180.0f;

        // ── 左ペイン：セクション一覧 ──
        ImGui::BeginChild("##settings_left", ImVec2(leftPaneW, 0.0f), true);
        {
            EnginePanelEntry* firstVisible = nullptr;
            bool selectionVisible = false;
            for (auto& p : enginePanels_) {
                if (p.category != EnginePanelCategory::Settings) continue;
                if (!matchesFilter(p.label)) continue;
                if (!firstVisible) firstVisible = &p;

                const bool selected = (p.label == selectedSettingsLabel_);
                selectionVisible |= selected;
                if (ImGui::Selectable(p.label.c_str(), selected)) {
                    selectedSettingsLabel_ = p.label;
                    selectionVisible = true;
                }
            }
            // 未選択・選択セクションが絞り込みで消えた場合は先頭を選ぶ
            if (!selectionVisible && firstVisible) {
                selectedSettingsLabel_ = firstVisible->label;
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // ── 右ペイン：選択セクションの内容 ──
        ImGui::BeginChild("##settings_right", ImVec2(0.0f, 0.0f), false);
        {
            EnginePanelEntry* selectedEntry = nullptr;
            for (auto& p : enginePanels_) {
                if (p.category != EnginePanelCategory::Settings) continue;
                if (p.label == selectedSettingsLabel_) { selectedEntry = &p; break; }
            }

            if (selectedEntry && selectedEntry->drawer) {
                ImGui::SeparatorText(selectedEntry->label.c_str());
                selectedEntry->drawer();
            } else {
                ImGui::TextDisabled("セクションがありません");
            }
        }
        ImGui::EndChild();

        ImGui::End();
    }

    void GameDebugUI::RegisterWindowsForDocking()
    {
        if (!dockingUI_) return;

        dockingUI_->RegisterWindow("Hierarchy", DockArea::Hierarchy);
        dockingUI_->RegisterWindow("Inspector", DockArea::Right);
        dockingUI_->RegisterWindow(consoleWindow, DockArea::Bottom);
        dockingUI_->RegisterWindow("Project",     DockArea::Bottom);

        // エンジンパネルはドッキング登録しない
        //（Settings は Engine Settings ウィンドウ内、Tools はフローティングで開く）
    }
}
#endif // USE_IMGUI
