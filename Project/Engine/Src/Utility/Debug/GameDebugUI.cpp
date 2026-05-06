#include "GameDebugUI.h"

#ifdef USE_IMGUI
#include "Utility/Debug/ImGui/DockingUI.h"
#include "EngineSystem/EngineSystem.h"
#include "EngineSystem/EngineConfig.h"
#include "Utility/FrameRate/FrameRateController.h"
#include <imgui.h>

#include <algorithm>


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
        // Camera Editorをエンジンパネルとして登録（独立ウィンドウ、他パネルと統一）
        RegisterEnginePanel("Camera Editor", std::move(callback));
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

    void GameDebugUI::RegisterEnginePanel(const std::string& label, std::function<void()> drawer)
    {
        for (auto& p : enginePanels_) {
            if (p.label == label) { p.drawer = std::move(drawer); return; }
        }
        enginePanels_.push_back({ label, std::move(drawer), false });

        // ドッキングシステムにウィンドウを登録
        if (dockingUI_) {
            dockingUI_->RegisterWindow(label, DockArea::Right);
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
            if (ImGui::BeginMenu("Debug")) {
                ImGui::Checkbox("Console", &showConsole_);
                ImGui::EndMenu();
            }

            // EngineDebug 専用メニュー（Inspectorタブとしてトグル・複数選択対応）
            if (ImGui::BeginMenu("EngineDebug")) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f, 0.60f, 0.60f, 1.0f));
                ImGui::TextUnformatted("  デバッグ情報");
                ImGui::PopStyleColor();
                ImGui::Separator();
                for (auto& entry : engineEditors_) {
                    // カテゴリ別アイコン
                    const char* icon = "  ";
                    if (entry.label == "パフォーマンス")   icon = " \xE2\x96\xB6  ";
                    else if (entry.label == "レンダリング") icon = " \xE2\x97\x86  ";
                    else if (entry.label == "シーン")       icon = " \xE2\x97\x89  ";
                    else if (entry.label == "リソース")     icon = " \xE2\x96\xA0  ";
                    else if (entry.label == "メモリ")       icon = " \xE2\x96\xA3  ";
                    std::string itemLabel = std::string(icon) + entry.label;

                    // DontClosePopups でクリックしてもメニューを閉じない
                    if (ImGui::Selectable(itemLabel.c_str(), entry.visible,
                            ImGuiSelectableFlags_DontClosePopups,
                            ImVec2(0.0f, 0.0f)))
                    {
                        entry.visible = !entry.visible;
                    }
                    // チェックマークを右端に描画
                    if (entry.visible) {
                        ImGui::SameLine();
                        float checkX = ImGui::GetWindowWidth()
                            - ImGui::GetStyle().WindowPadding.x
                            - ImGui::CalcTextSize("\xE2\x9C\x93").x;
                        ImGui::SetCursorPosX(checkX);
                        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "\xE2\x9C\x93");
                    }
                }
                ImGui::Separator();
                // 全て表示 / 全て非表示（こちらも閉じない）
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.65f, 0.0f, 1.0f));
                if (ImGui::Selectable("  すべて表示", false, ImGuiSelectableFlags_DontClosePopups)) {
                    for (auto& e : engineEditors_) e.visible = true;
                }
                if (ImGui::Selectable("  すべて非表示", false, ImGuiSelectableFlags_DontClosePopups)) {
                    for (auto& e : engineEditors_) e.visible = false;
                }
                ImGui::PopStyleColor();
                ImGui::EndMenu();
            }

            // Window Manager パネルの開閉トグル
            ImGui::MenuItem("Window", nullptr, &showEditorSwitcher_);

            if (dockingUI_ && ImGui::BeginMenu("Layout")) {
                const DockLayoutPreset currentLayout = dockingUI_->GetLayoutPreset();
                const bool isStandard = (currentLayout == DockLayoutPreset::Standard);
                const bool isUnity2By3 = (currentLayout == DockLayoutPreset::TwoByThree);

                if (ImGui::MenuItem("Standard", nullptr, isStandard)) {
                    dockingUI_->SetLayoutPreset(DockLayoutPreset::Standard);
                }

                if (ImGui::MenuItem("Unity 2 by 3", nullptr, isUnity2By3)) {
                    dockingUI_->SetLayoutPreset(DockLayoutPreset::TwoByThree);
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
        DrawEditorSwitcherPanel();

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
        if (auto w = UI::Scope::WindowScope("Hierarchy")) {
                if (auto tabBar = UI::Scope::TabBarScope("##HierarchyTabs")) {
                    if (auto tab = UI::Scope::TabItemScope("Objects")) {
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

    void GameDebugUI::DrawInspectorPanel()
    {
        if (auto w = UI::Scope::WindowScope("Inspector")) {
            if (auto tabBar = UI::Scope::TabBarScope("##InspectorTabs", ImGuiTabBarFlags_AutoSelectNewTabs)) {
                // Object タブ（常時表示、閉じるボタンなし）
                if (auto tab = UI::Scope::TabItemScope("Object")) {
                    if (inspectorObjectDrawer_) {
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
        for (auto& panel : enginePanels_) {
            if (!panel.visible) continue;
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
            ImGui::SetNextWindowSize(ImVec2(420.0f, 480.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin(panel.label.c_str(), &panel.visible)) {
                if (panel.drawer) panel.drawer();
            }
            ImGui::End();
        }
    }

    void GameDebugUI::DrawEditorSwitcherPanel()
    {
        if (!showEditorSwitcher_) return;

        ImGui::SetNextWindowSizeConstraints(ImVec2(280, 0), ImVec2(400, 800));
        if (!ImGui::Begin("Window Manager", &showEditorSwitcher_, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::End();
            return;
        }

        const ImVec4 kEngineColor = ImVec4(0.35f, 0.65f, 1.0f, 1.0f);
        const ImVec4 kAppColor    = ImVec4(0.45f, 0.85f, 0.45f, 1.0f);
        const ImVec4 kPanelColor  = ImVec4(0.85f, 0.65f, 0.25f, 1.0f);

        float toggleW = ImGui::GetFrameHeight() * 1.8f;

        auto drawEditorTable = [toggleW](const char* tableId, auto& entries, const ImVec4& dotColor) {
            if (auto table = UI::Scope::TableScope(tableId, 2)) {
                ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Switch", ImGuiTableColumnFlags_WidthFixed, toggleW);

                for (auto& entry : entries) {
                    ImGui::PushID(entry.label.c_str());
                    ImGui::TableNextRow();

                    // ラベル列（色付きドット + 名前）
                    ImGui::TableNextColumn();
                    {
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        ImVec2 pos = ImGui::GetCursorScreenPos();
                        float cy = pos.y + ImGui::GetFrameHeight() * 0.5f;
                        dl->AddCircleFilled(ImVec2(pos.x + 4.0f, cy), 3.5f,
                            ImGui::GetColorU32(dotColor));
                        ImGui::Indent(14.0f);
                        ImGui::AlignTextToFramePadding();
                        ImGui::Text("%s", entry.label.c_str());
                        ImGui::Unindent(14.0f);
                    }

                    // トグルスイッチ列
                    ImGui::TableNextColumn();
                    UI::Widgets::ToggleSwitch("##sw", &entry.visible);

                    ImGui::PopID();
                }
            }
        };

        // ── 独立パネル（Lighting, Post Effects 等） ──
        if (!enginePanels_.empty()) {
            UI::SectionHeader("Panels");
            drawEditorTable("##PanelTable", enginePanels_, kPanelColor);
            UI::Spacing();
        }

        // ── Inspector タブ（エンジン + アプリエディター） ──
        if (!engineEditors_.empty() || !appEditors_.empty()) {
            UI::SectionHeader("Inspector");

            if (!engineEditors_.empty()) {
                UI::Hint("Engine");
                drawEditorTable("##EngineTable", engineEditors_, kEngineColor);
            }

            if (!appEditors_.empty()) {
                if (!engineEditors_.empty()) { UI::Spacing(); }
                UI::Hint("Application");
                drawEditorTable("##AppTable", appEditors_, kAppColor);
            }
        }

        ImGui::End();
    }

    void GameDebugUI::RegisterWindowsForDocking()
    {
        if (!dockingUI_) return;

        dockingUI_->RegisterWindow("Hierarchy", DockArea::Hierarchy);
        dockingUI_->RegisterWindow("Inspector", DockArea::Right);
        dockingUI_->RegisterWindow(consoleWindow, DockArea::Bottom);
        dockingUI_->RegisterWindow("Project",     DockArea::Bottom);

        // Initialize時に登録済みのエンジンパネルをドッキングに追加
        for (const auto& panel : enginePanels_) {
            dockingUI_->RegisterWindow(panel.label, DockArea::Right);
        }
    }
}
#endif // USE_IMGUI
