#include "GameDebugUI.h"

#ifdef USE_IMGUI
#include "Utility/Debug/ImGui/DockingUI.h"
#include "EngineSystem/EngineSystem.h"
#include "Utility/FrameRate/FrameRateController.h"
#include "Scene/SceneManager.h"

#include <Psapi.h>
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

        // Lightingをエンジン専用パネルとして登録（独立ウィンドウ）
        RegisterEnginePanel("Lighting", [this]() {
            auto lightManager = engine_->GetComponent<LightManager>();
            if (lightManager) {
                lightManager->DrawAllImGui();
            }
        });

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
        // 既存の "Camera Editor" エントリがあればコールバックを更新するだけにする
        // （シーン切り替え時に重複登録されないようにする）
        for (auto& [label, drawer] : engineEditors_) {
            if (label == "Camera Editor") {
                drawer = std::move(callback);
                return;
            }
        }
        // Camera Editorは先頭に挿入（Lightingの前に表示）
        engineEditors_.insert(engineEditors_.begin(),
            std::make_pair(std::string("Camera Editor"), std::move(callback)));
    }

    void GameDebugUI::RegisterAppEditor(const std::string& label, std::function<void()> drawer)
    {
        appEditors_.emplace_back(label, std::move(drawer));
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

    void GameDebugUI::Update()
    {
        // メニューバーと他のパネルをまとめて呼び出す
        ShowMainMenuBar();
        UpdateDebugPanels();
    }

    void GameDebugUI::ShowMainMenuBar()
    {
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("Debug")) {
                ImGui::Checkbox("Console", &showConsole_);
                ImGui::EndMenu();
            }

            // エンジン専用デバッグパネル（独立ウィンドウ）
            if (ImGui::BeginMenu("Engine")) {
                if (enginePanels_.empty()) {
                    UI::Hint("登録済みのパネルがありません");
                } else {
                    for (auto& panel : enginePanels_) {
                        ImGui::Checkbox(panel.label.c_str(), &panel.visible);
                    }
                }
                ImGui::EndMenu();
            }

            // Inspector内エディター（Camera Editor + App Editor）
            if (ImGui::BeginMenu("Editor")) {
                // Camera Editor 等（エンジンエディター）
                for (auto& [label, _] : engineEditors_) {
                    bool isActive = (activeEditorId_ == label);
                    if (ImGui::Checkbox(label.c_str(), &isActive)) {
                        activeEditorId_ = isActive ? label : "";
                    }
                }
                if (!engineEditors_.empty() && !appEditors_.empty()) {
                    ImGui::Separator();
                }
                // App Editor
                if (ImGui::BeginMenu("App Editor")) {
                    if (appEditors_.empty()) {
                        UI::Hint("登録済みのエディターがありません");
                    } else {
                        for (auto& [label, _] : appEditors_) {
                            bool isActive = (activeEditorId_ == label);
                            if (ImGui::Checkbox(label.c_str(), &isActive)) {
                                activeEditorId_ = isActive ? label : "";
                            }
                        }
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }

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

            ImGui::EndMainMenuBar();
        }
    }

    void GameDebugUI::UpdateDebugPanels()
    {
        DrawHierarchyPanel();
        DrawInspectorPanel();
        DrawEnginePanels();

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
            if (inspectorObjectDrawer_) {
                inspectorObjectDrawer_();
            } else {
                UI::Hint("シーンが読み込まれていません");
            }

            if (!activeEditorId_.empty()) {
                UI::Separator();

                const auto drawActive = [this](const EditorList& list) -> bool {
                    for (const auto& [label, drawer] : list) {
                        if (label != activeEditorId_) continue;
                        std::string breadcrumb = "Inspector > ";
                        breadcrumb += label;
                        UI::SectionHeader(breadcrumb.c_str());
                        if (drawer) drawer();
                        return true;
                    }
                    return false;
                };
                if (!drawActive(engineEditors_)) {
                    drawActive(appEditors_);
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
