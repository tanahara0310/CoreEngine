#include "pch.h"
#include "GameDebugUI.h"

#ifdef USE_IMGUI
#include "Editor/ImGui/DockingUI.h"
#include "EngineSystem/EngineSystem.h"
#include "EngineSystem/EngineConfig.h"
#include "Utility/FrameRate/FrameRateController.h"
#include <imgui.h>

#include <algorithm>
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

            // Window Manager パネルの開閉トグル
            ImGui::MenuItem("Window", nullptr, &showEditorSwitcher_);

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

        ImGui::SetNextWindowSize(ImVec2(520.0f, 360.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(360.0f, 200.0f), ImVec2(900.0f, 900.0f));
        if (!ImGui::Begin("Window Manager", &showEditorSwitcher_)) {
            ImGui::End();
            return;
        }

        // ── カテゴリ定義 ──────────────────────────────────────────
        struct Category {
            const char* label;
            const char* icon;
        };
        static constexpr Category kCategories[] = {
            { "Panels",    "[ ]" },
            { "Inspector", "[ ]" },
        };
        static constexpr int kCategoryCount = static_cast<int>(std::size(kCategories));

        const ImVec4 kEngineColor = ImVec4(0.35f, 0.65f, 1.0f,  1.0f);
        const ImVec4 kAppColor    = ImVec4(0.45f, 0.85f, 0.45f, 1.0f);
        const ImVec4 kPanelColor  = ImVec4(0.85f, 0.65f, 0.25f, 1.0f);
        const ImVec4 kSelBg       = ImVec4(0.22f, 0.22f, 0.27f, 1.0f);
        const ImVec4 kHoverBg     = ImVec4(0.18f, 0.18f, 0.22f, 1.0f);
        const ImVec4 kHeaderCol   = ImVec4(1.0f,  0.65f, 0.0f,  1.0f);

        const float leftPaneW  = 110.0f;
        const float totalH     = ImGui::GetContentRegionAvail().y;

        // ── 左ペイン：カテゴリリスト ──────────────────────────────
        ImGui::BeginChild("##wm_left", ImVec2(leftPaneW, totalH), false,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,  ImVec2(0.0f, 2.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 6.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

            for (int i = 0; i < kCategoryCount; ++i)
            {
                const bool selected = (selectedCategory_ == i);
                if (selected)
                    ImGui::PushStyleColor(ImGuiCol_Button,        kSelBg);
                else
                    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));

                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kHoverBg);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  kSelBg);

                char lbl[64];
                snprintf(lbl, sizeof(lbl), "%s##cat%d", kCategories[i].label, i);
                if (ImGui::Button(lbl, ImVec2(leftPaneW - 4.0f, 0.0f)))
                    selectedCategory_ = i;

                ImGui::PopStyleColor(3);
            }
            ImGui::PopStyleVar(3);
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // セパレータ
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 p = ImGui::GetCursorScreenPos();
            dl->AddLine(ImVec2(p.x, p.y), ImVec2(p.x, p.y + totalH),
                ImGui::GetColorU32(ImVec4(0.35f, 0.35f, 0.35f, 1.0f)));
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f);
        }

        // ── 右ペイン：選択カテゴリのコンテンツ ────────────────────
        ImGui::BeginChild("##wm_right", ImVec2(0.0f, totalH), false);
        {
            const float toggleW = ImGui::GetFrameHeight() * 1.8f;

            auto drawToggleTable = [&](const char* tableId, auto& entries, const ImVec4& dotColor) {
                if (entries.empty()) {
                    ImGui::TextDisabled("（登録なし）");
                    return;
                }
                ImGuiTableFlags tableFlags =
                    ImGuiTableFlags_SizingStretchProp |
                    ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_BordersInnerH |
                    ImGuiTableFlags_PadOuterX;
                if (ImGui::BeginTable(tableId, 2, tableFlags))
                {
                    ImGui::TableSetupColumn("Label",  ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Switch", ImGuiTableColumnFlags_WidthFixed, toggleW);

                    for (auto& entry : entries)
                    {
                        ImGui::PushID(entry.label.c_str());
                        ImGui::TableNextRow();
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
                        ImGui::TableNextColumn();
                        UI::Widgets::ToggleSwitch("##sw", &entry.visible);
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
            };

            if (selectedCategory_ == 0)
            {
                // ── Panels ──
                ImGui::TextColored(kHeaderCol, "Panels");
                ImGui::Separator();
                ImGui::Spacing();
                drawToggleTable("##PanelTable", enginePanels_, kPanelColor);
            }
            else if (selectedCategory_ == 1)
            {
                // ── Inspector ──
                ImGui::TextColored(kHeaderCol, "Inspector");
                ImGui::Separator();
                ImGui::Spacing();

                if (!engineEditors_.empty())
                {
                    UI::Hint("Engine");
                    drawToggleTable("##EngineTable", engineEditors_, kEngineColor);
                    ImGui::Spacing();
                }
                if (!appEditors_.empty())
                {
                    UI::Hint("Application");
                    drawToggleTable("##AppTable", appEditors_, kAppColor);
                }
                if (engineEditors_.empty() && appEditors_.empty())
                    ImGui::TextDisabled("（登録なし）");
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

        // Initialize時に登録済みのエンジンパネルをドッキングに追加
        for (const auto& panel : enginePanels_) {
            dockingUI_->RegisterWindow(panel.label, DockArea::Right);
        }
    }
}
#endif // USE_IMGUI
