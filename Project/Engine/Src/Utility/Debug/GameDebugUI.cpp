#include "pch.h"
#include "GameDebugUI.h"

#ifdef USE_IMGUI
#include "Editor/ImGui/DockingUI.h"
#include "Editor/Panel/EditorPanelRegistry.h"
#include "Editor/Scene/SceneDebugEditor.h"
#include "EngineSystem/EngineSystem.h"
#include "EngineSystem/EngineConfig.h"
#include "EngineSystem/PlaybackState.h"
#include "Utility/FrameRate/FrameRateController.h"
#include "Utility/FrameRate/Time.h"
#include "WinApp/WinApp.h"
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

        // 単独ウィンドウは開くたびに Game ビューの真上へ浮くので、既定のドック先を与える。
        // レジストリへ既に積まれているぶんもここでまとめて通知される。
        // （マルチビューポートが有効なので、広いパネルはここから別モニタへ引き出せばよい）
        Editor::EditorPanelRegistry::Get().SetDockRegistrar(
            [this](const std::string& id) {
                if (dockingUI_) {
                    dockingUI_->RegisterWindow(id, DockArea::Right);
                }
            });

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
            // Window メニュー：すべてのパネルを用途別サブメニューへ振り分ける。
            // パネルが増えても一覧が縦に伸び続けないようにするため、
            // 直下に並べるのは「常に使うもの」だけに絞る。
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
                            };
                    case Editor::PanelGroup::Analysis:
                        return [&tabMenu]() {
                            tabMenu(Editor::PanelGroup::Analysis, "Engine Stats");
                            };
                    case Editor::PanelGroup::Editor:
                        return [this]() {
                            ImGui::MenuItem("Engine Settings", nullptr, &showEngineSettings_);
                            };
                    default:
                        return nullptr;
                    }
                    };

                // 並び順は kPanelGroupOrder に一本化（Engine Settings の一覧と同じ順序になる）
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

            // 再生 / 停止（メニューバー中央）
            DrawPlaybackControls();

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

    void GameDebugUI::DrawPlaybackControls()
    {
        auto& playback = PlaybackStateManager::GetInstance();
        const bool playing = playback.IsPlaying();

        // 先にボタン 2 つ分の幅を測り、メニューバーのちょうど中央から並べ始める。
        // 高さを指定しないボタンはメニューバーと同じ高さ（GetFrameHeight）になる
        const ImGuiStyle& style = ImGui::GetStyle();
        const float buttonWidth = ImGui::GetFrameHeight() * 1.6f;
        const float totalWidth = buttonWidth * 2.0f + style.ItemSpacing.x;

        // SameLine の引数は「内容の開始位置からの相対」なので、メニューバー左端の
        // 安全域（DisplaySafeAreaPadding）を引いてウィンドウ中央へ正確に合わせる
        ImGui::SameLine((ImGui::GetWindowWidth() - totalWidth) * 0.5f - ImGui::GetCursorStartPos().x);

        // 現在の状態側のボタンだけをアクセント色で塗る（Unity の再生ボタンと同じ見せ方）
        const auto stateButton = [buttonWidth](const char* label, bool current, const char* tooltip) {
            const ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive);
            if (current) {
                ImGui::PushStyleColor(ImGuiCol_Button, accent);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accent);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, accent);
            }
            const bool pressed = ImGui::Button(label, ImVec2(buttonWidth, 0.0f));
            if (current) {
                ImGui::PopStyleColor(3);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", tooltip);
            }
            return pressed;
            };

        if (stateButton("▶##Play", playing, "再生\nゲームの更新を再開します")) {
            playback.Play();
        }

        ImGui::SameLine();

        if (stateButton("■##Stop", !playing,
            "停止\nゲームの更新を止めます。\n"
            "止めている間もカメラ・ギズモ・各パネルは動くので、\n"
            "パラメータはそのまま編集できます。")) {
            playback.Stop();
        }

        // 色が変わるだけだと見落とすので、止まっていることは文字でも出す
        if (!playing) {
            ImGui::SameLine();
            ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_PlotHistogram), "停止中");
        }
    }

    void GameDebugUI::DrawPanelGroupMenu(Editor::PanelGroup group, const char* label,
        const std::function<void()>& extraContent)
    {
        auto& registry = Editor::EditorPanelRegistry::Get();

        // 単独ウィンドウとして開けるものだけを集める
        //（SettingsSection は Engine Settings ウィンドウ内なのでここには出さない）
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
        DrawHierarchyPanel();
        DrawInspectorPanel();
        DrawPanelWindows();
        DrawEngineSettingsWindow();

        if (showConsole_) ShowConsoleUI();

        if (dockingUI_) {
            float fps = 0.0f;
            const float deltaTimeMs = Time::UnscaledDeltaTime() * 1000.0f;
            if (auto* frameRate = engine_->GetService<FrameRateController>()) {
                fps = frameRate->GetCurrentFPS();
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
                    if (env && env->desc.draw) {
                        ImGui::SeparatorText(env->Id().c_str());
                        env->desc.draw();
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

    void GameDebugUI::DrawEngineSettingsWindow()
    {
        if (!showEngineSettings_) return;

        ImGui::SetNextWindowSize(ImVec2(780.0f, 560.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(480.0f, 280.0f), ImVec2(1600.0f, 1200.0f));
        if (!ImGui::Begin("Engine Settings", &showEngineSettings_)) {
            ImGui::End();
            return;
        }

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

        const float leftPaneW = 210.0f;

        // ── 左ペイン：カテゴリ別のセクション一覧 ──
        ImGui::BeginChild("##settings_left", ImVec2(leftPaneW, 0.0f), ImGuiChildFlags_Borders);
        {
            // 検索ボックスは一覧の上に置く（絞り込み対象が一覧であることを明示する）
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##settings_filter", "検索...", settingsFilter_, sizeof(settingsFilter_));
            ImGui::Spacing();

            auto& registry = Editor::EditorPanelRegistry::Get();
            Editor::EditorPanel* firstVisible = nullptr;
            bool selectionVisible = false;

            for (const auto& [group, groupLabel] : Editor::kPanelGroupOrder) {
                // このカテゴリに表示対象があるかを先に調べ、無ければ見出しごと出さない
                const bool hasAny = registry.Any(Editor::PanelPlacement::SettingsSection,
                    [&](const Editor::EditorPanel& entry) {
                        return entry.desc.group == group && matchesFilter(entry.Id());
                    });
                if (!hasAny) {
                    continue;
                }

                ImGui::SeparatorText(groupLabel);

                registry.ForEach(Editor::PanelPlacement::SettingsSection,
                    [&](Editor::EditorPanel& panel) {
                        if (panel.desc.group != group) return;
                        if (!matchesFilter(panel.Id())) return;
                        if (!firstVisible) firstVisible = &panel;

                        const bool selected = (panel.Id() == selectedSettingsLabel_);
                        selectionVisible |= selected;

                        ImGui::Indent(6.0f);
                        if (ImGui::Selectable(panel.Id().c_str(), selected)) {
                            selectedSettingsLabel_ = panel.Id();
                            selectionVisible = true;
                        }
                        ImGui::Unindent(6.0f);
                    });
            }

            if (!firstVisible) {
                ImGui::TextDisabled("該当なし");
            }
            // 未選択・選択セクションが絞り込みで消えた場合は先頭を選ぶ
            else if (!selectionVisible) {
                selectedSettingsLabel_ = firstVisible->Id();
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // ── 右ペイン：選択セクションの内容 ──
        ImGui::BeginChild("##settings_right", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
        {
            Editor::EditorPanel* selectedEntry =
                Editor::EditorPanelRegistry::Get().Find(selectedSettingsLabel_);
            if (selectedEntry
                && selectedEntry->desc.placement != Editor::PanelPlacement::SettingsSection) {
                selectedEntry = nullptr;
            }

            if (selectedEntry && selectedEntry->desc.draw) {
                // 見出しは「カテゴリ / セクション名」のパンくずにして、今どこを見ているか分かるようにする
                ImGui::TextDisabled("%s", Editor::ToDisplayName(selectedEntry->desc.group));
                ImGui::SameLine(0.0f, 6.0f);
                ImGui::TextDisabled("/");
                ImGui::SameLine(0.0f, 6.0f);
                ImGui::TextUnformatted(selectedEntry->Id().c_str());
                ImGui::Separator();
                ImGui::Spacing();

                selectedEntry->desc.draw();
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
