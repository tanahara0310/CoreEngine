#include "pch.h"
#include "GameDebugUI.h"

#ifdef USE_IMGUI
#include "Editor/ImGui/DockingUI.h"
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
        // Camera Editor は単独ウィンドウ。エディタ視点カメラの設定なので Editor グループへ。
        RegisterEnginePanel("Camera Editor", std::move(callback),
            EnginePanelCategory::Tools, EnginePanelGroup::Editor);
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
        EnginePanelCategory category, EnginePanelGroup group)
    {
        for (auto& p : enginePanels_) {
            if (p.label == label) {
                p.drawer = std::move(drawer);
                p.category = category;
                p.group = group;
                return;
            }
        }
        enginePanels_.push_back({ label, std::move(drawer), false, category, group });

        // Settings は Engine Settings ウィンドウ内のセクションなので独立ウィンドウを持たない。
        // Tools は独立ウィンドウとして開くため、既定のドッキング先を与えておく。
        // これが無いと Game ビューの真上に浮き、パネルを開くほど画面が覆われる。
        // （マルチビューポートが有効なので、広いパネルはここから別モニタへ引き出せばよい）
        if (category == EnginePanelCategory::Tools && dockingUI_) {
            dockingUI_->RegisterWindow(label, DockArea::Right);
        }
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

    void GameDebugUI::RegisterEngineDebugPanel(const std::string& label, std::function<void()> drawer,
        EnginePanelGroup group)
    {
        for (auto& p : engineDebugPanels_) {
            if (p.label == label) { p.drawer = std::move(drawer); p.group = group; return; }
        }
        engineDebugPanels_.push_back({ label, std::move(drawer), false, EnginePanelCategory::Tools, group });

        // Tools パネルと同じ理由で、デバッグ情報パネルにも既定のドッキング先を与える
        if (dockingUI_) {
            dockingUI_->RegisterWindow(label, DockArea::Right);
        }
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
            // Window メニュー：すべてのパネルを用途別サブメニューへ振り分ける。
            // パネルが増えても一覧が縦に伸び続けないようにするため、
            // 直下に並べるのは「常に使うもの」だけに絞る。
            if (ImGui::BeginMenu("Window")) {

                // グループごとの追加項目（そのグループにしか無いもの）
                const auto drawExtra = [this](EnginePanelGroup group) -> std::function<void()> {
                    switch (group) {
                    case EnginePanelGroup::General:
                        return [this]() {
                            ImGui::MenuItem("Hierarchy", nullptr, &showHierarchy_);
                            ImGui::MenuItem("Inspector", nullptr, &showInspector_);
                            ImGui::MenuItem("Console", nullptr, &showConsole_);
                            };
                    case EnginePanelGroup::Analysis:
                        return [this]() {
                            if (!engineEditors_.empty() && ImGui::BeginMenu("Engine Stats")) {
                                for (auto& entry : engineEditors_) {
                                    ImGui::MenuItem(entry.label.c_str(), nullptr, &entry.visible);
                                }
                                ImGui::EndMenu();
                            }
                            };
                    case EnginePanelGroup::Editor:
                        return [this]() {
                            ImGui::MenuItem("Engine Settings", nullptr, &showEngineSettings_);
                            };
                    default:
                        return nullptr;
                    }
                    };

                // 並び順は kEnginePanelGroupOrder に一本化（Engine Settings の一覧と同じ順序になる）
                for (const auto& [group, groupLabel] : kEnginePanelGroupOrder) {
                    DrawPanelGroupMenu(group, groupLabel, drawExtra(group));
                }

                // ── アプリ固有のエディタ ──
                if (!appEditors_.empty() && ImGui::BeginMenu("Application")) {
                    for (auto& entry : appEditors_) {
                        ImGui::MenuItem(entry.label.c_str(), nullptr, &entry.visible);
                    }
                    ImGui::EndMenu();
                }

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

    void GameDebugUI::DrawPanelGroupMenu(EnginePanelGroup group, const char* label,
        const std::function<void()>& extraContent)
    {
        // 単独ウィンドウとして開けるものだけを集める
        //（Settings カテゴリは Engine Settings ウィンドウ内のセクションなのでここには出さない）
        const auto hasToolPanel = [this, group] {
            return std::any_of(enginePanels_.begin(), enginePanels_.end(),
                [group](const EnginePanelEntry& entry) {
                    return entry.category == EnginePanelCategory::Tools && entry.group == group;
                });
            };
        const auto hasDebugPanel = [this, group] {
            return std::any_of(engineDebugPanels_.begin(), engineDebugPanels_.end(),
                [group](const EnginePanelEntry& entry) { return entry.group == group; });
            };

        // 中身が何も無いグループはメニュー項目自体を出さない（空のサブメニューを作らない）
        if (!extraContent && !hasToolPanel() && !hasDebugPanel()) {
            return;
        }

        if (!ImGui::BeginMenu(label)) {
            return;
        }

        if (extraContent) {
            extraContent();
            if (hasToolPanel() || hasDebugPanel()) {
                ImGui::Separator();
            }
        }

        for (auto& panel : enginePanels_) {
            if (panel.category != EnginePanelCategory::Tools || panel.group != group) {
                continue;
            }
            ImGui::MenuItem(panel.label.c_str(), nullptr, &panel.visible);
        }

        for (auto& panel : engineDebugPanels_) {
            if (panel.group != group) {
                continue;
            }
            ImGui::MenuItem(panel.label.c_str(), nullptr, &panel.visible);
        }

        ImGui::EndMenu();
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

            EnginePanelEntry* firstVisible = nullptr;
            bool selectionVisible = false;

            for (const auto& [group, groupLabel] : kEnginePanelGroupOrder) {
                // このカテゴリに表示対象があるかを先に調べ、無ければ見出しごと出さない
                const bool hasAny = std::any_of(enginePanels_.begin(), enginePanels_.end(),
                    [&](const EnginePanelEntry& entry) {
                        return entry.category == EnginePanelCategory::Settings
                            && entry.group == group && matchesFilter(entry.label);
                    });
                if (!hasAny) {
                    continue;
                }

                ImGui::SeparatorText(groupLabel);

                for (auto& panel : enginePanels_) {
                    if (panel.category != EnginePanelCategory::Settings) continue;
                    if (panel.group != group) continue;
                    if (!matchesFilter(panel.label)) continue;
                    if (!firstVisible) firstVisible = &panel;

                    const bool selected = (panel.label == selectedSettingsLabel_);
                    selectionVisible |= selected;

                    ImGui::Indent(6.0f);
                    if (ImGui::Selectable(panel.label.c_str(), selected)) {
                        selectedSettingsLabel_ = panel.label;
                        selectionVisible = true;
                    }
                    ImGui::Unindent(6.0f);
                }
            }

            if (!firstVisible) {
                ImGui::TextDisabled("該当なし");
            }
            // 未選択・選択セクションが絞り込みで消えた場合は先頭を選ぶ
            else if (!selectionVisible) {
                selectedSettingsLabel_ = firstVisible->label;
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // ── 右ペイン：選択セクションの内容 ──
        ImGui::BeginChild("##settings_right", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
        {
            EnginePanelEntry* selectedEntry = nullptr;
            for (auto& p : enginePanels_) {
                if (p.category != EnginePanelCategory::Settings) continue;
                if (p.label == selectedSettingsLabel_) { selectedEntry = &p; break; }
            }

            if (selectedEntry && selectedEntry->drawer) {
                // 見出しは「カテゴリ / セクション名」のパンくずにして、今どこを見ているか分かるようにする
                ImGui::TextDisabled("%s", ToDisplayName(selectedEntry->group));
                ImGui::SameLine(0.0f, 6.0f);
                ImGui::TextDisabled("/");
                ImGui::SameLine(0.0f, 6.0f);
                ImGui::TextUnformatted(selectedEntry->label.c_str());
                ImGui::Separator();
                ImGui::Spacing();

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
