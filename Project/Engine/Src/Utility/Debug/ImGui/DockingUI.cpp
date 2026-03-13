#include "DockingUI.h"
#include "Graphics/Texture/TextureManager.h"
#include "EngineSystem/PlaybackState.h"
#include "Utility/Logger/Logger.h"
#include <format>


namespace CoreEngine
{
void DockingUI::RegisterWindow(const std::string& windowName, DockArea area)
{
    registeredWindows_[windowName] = area;
    
    // レイアウトが既に初期化されている場合、動的にドッキング
    if (layoutInitialized_) {
        ImGuiID nodeId = ResolveNodeIdForWindow(windowName, area);
        if (nodeId != 0) {
            ImGui::DockBuilderDockWindow(windowName.c_str(), nodeId);
        }
    }
}

void DockingUI::UnregisterWindow(const std::string& windowName)
{
    registeredWindows_.erase(windowName);
}

void DockingUI::BeginDockSpaceHostWindow()
{
    // メインビューポートに合わせてホストウィンドウの位置・サイズを設定
    ImGuiViewport* vp = ImGui::GetMainViewport();

    // メニューバーの高さを取得
    float menuBarHeight = ImGui::GetFrameHeight();

    // 再生ツールバーを描画（メニューバーの直下）
    DrawPlaybackToolbar();

    // メニューバー + ツールバーの下にドッキングエリアを配置
    float totalTopHeight = menuBarHeight + toolbarHeight_;
    ImVec2 pos = vp->Pos;
    pos.y += totalTopHeight;

    ImVec2 size = vp->Size;
    size.y -= totalTopHeight;

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::SetNextWindowViewport(vp->ID);

    // タイトルバーや移動不可など、ドッキング用の特殊フラグを設定
    ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoTitleBar
        | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoDocking
        | ImGuiWindowFlags_NoBringToFrontOnFocus
        | ImGuiWindowFlags_NoNavFocus;

    // 見た目調整（角丸・枠線・余白）を0にして全面ホストウィンドウ化
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("##DockSpaceHost", nullptr, hostFlags);
    ImGui::PopStyleVar(3);

    // ドッキングスペースを作成（中央透過）
    ImGuiID dockId = ImGui::GetID("MyDockSpace");
    ImGui::DockSpace(dockId, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);
}

void DockingUI::SetupDockSpace()
{
    // 初回またはレイアウト変更時のみドッキングレイアウトを構築
    if (layoutInitialized_ && !layoutDirty_)
        return;

    BuildDockLayout();
    layoutInitialized_ = true;
    layoutDirty_ = false;
}

void DockingUI::SetLayoutPreset(DockLayoutPreset preset)
{
    if (layoutPreset_ == preset) {
        return;
    }

    layoutPreset_ = preset;
    layoutDirty_ = true;
}

ImGuiID DockingUI::GetNodeIdForArea(DockArea area) const
{
    return nodeIds_[static_cast<int>(area)];
}

ImGuiID DockingUI::ResolveNodeIdForWindow(const std::string& windowName, DockArea area) const
{
    if (layoutPreset_ == DockLayoutPreset::Unity2By3) {
        if (windowName == "Game") {
            return gameNodeId_;
        }

        if (windowName == "Scene") {
            return sceneNodeId_;
        }

        if ((area == DockArea::LeftTop || area == DockArea::LeftBottom || area == DockArea::Center) && toolNodeId_ != 0) {
            return toolNodeId_;
        }
    }

    return GetNodeIdForArea(area);
}

void DockingUI::BuildDockLayout()
{
    // ルートノード作成＆リセット
    ImGuiViewport* vp = ImGui::GetMainViewport();

    // メニューバー + ツールバーの高さを考慮したサイズを設定
    float menuBarHeight = ImGui::GetFrameHeight();
    float totalTopHeight = menuBarHeight + toolbarHeight_;
    ImVec2 dockSpaceSize = ImVec2(vp->Size.x, vp->Size.y - totalTopHeight);

    ImGuiID dockMain = ImGui::GetID("MyDockSpace");
    ImGui::DockBuilderRemoveNode(dockMain);
    ImGui::DockBuilderAddNode(dockMain, ImGuiDockNodeFlags_None);
    ImGui::DockBuilderSetNodeSize(dockMain, dockSpaceSize);

    for (ImGuiID& nodeId : nodeIds_) {
        nodeId = 0;
    }
    gameNodeId_ = 0;
    sceneNodeId_ = 0;
    toolNodeId_ = 0;

    if (layoutPreset_ == DockLayoutPreset::Unity2By3) {
        ImGuiID idViewportColumn, idRightColumn;
        ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.50f, &idRightColumn, &idViewportColumn);

        ImGuiID idGame, idScene;
        ImGui::DockBuilderSplitNode(idViewportColumn, ImGuiDir_Down, 0.50f, &idGame, &idScene);

        ImGuiID idProject, idRightTop;
        ImGui::DockBuilderSplitNode(idRightColumn, ImGuiDir_Down, 0.34f, &idProject, &idRightTop);

        ImGuiID idInspector, idToolColumn;
        ImGui::DockBuilderSplitNode(idRightTop, ImGuiDir_Right, 0.52f, &idInspector, &idToolColumn);

        gameNodeId_ = idGame;
        sceneNodeId_ = idScene;
        toolNodeId_ = idToolColumn;

        nodeIds_[static_cast<int>(DockArea::LeftTop)] = idToolColumn;
        nodeIds_[static_cast<int>(DockArea::LeftBottom)] = idToolColumn;
        nodeIds_[static_cast<int>(DockArea::Center)] = idToolColumn;
        nodeIds_[static_cast<int>(DockArea::Right)] = idInspector;
        nodeIds_[static_cast<int>(DockArea::BottomLeft)] = 0;
        nodeIds_[static_cast<int>(DockArea::BottomRight)] = 0;
        nodeIds_[static_cast<int>(DockArea::Bottom)] = idProject;
    } else {
        // 1) 右側エリア（Inspector）を最初に分割（25%）
        ImGuiID idMainArea, idRight;
        ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.25f, &idRight, &idMainArea);

        // 2) 残りのエリアを上下に分割（下部30%）
        ImGuiID idTop, idBottom;
        ImGui::DockBuilderSplitNode(idMainArea, ImGuiDir_Down, 0.30f, &idBottom, &idTop);

        // 3) 上部エリアを左側と中央に分割（左側25%）
        ImGuiID idLeft, idCenter;
        ImGui::DockBuilderSplitNode(idTop, ImGuiDir_Left, 0.25f, &idLeft, &idCenter);

        // 4) 左側をさらに上下に分割
        ImGuiID idLeftTop, idLeftBottom;
        ImGui::DockBuilderSplitNode(idLeft, ImGuiDir_Down, 0.5f, &idLeftBottom, &idLeftTop);

        nodeIds_[static_cast<int>(DockArea::LeftTop)] = idLeftTop;
        nodeIds_[static_cast<int>(DockArea::LeftBottom)] = idLeftBottom;
        nodeIds_[static_cast<int>(DockArea::Center)] = idCenter;
        nodeIds_[static_cast<int>(DockArea::Right)] = idRight;
        nodeIds_[static_cast<int>(DockArea::BottomLeft)] = 0;
        nodeIds_[static_cast<int>(DockArea::BottomRight)] = 0;
        nodeIds_[static_cast<int>(DockArea::Bottom)] = idBottom;
    }

    // 登録されているウィンドウを各ノードにドッキング
    for (const auto& [windowName, area] : registeredWindows_) {
        ImGuiID nodeId = ResolveNodeIdForWindow(windowName, area);
        if (nodeId != 0) {
            ImGui::DockBuilderDockWindow(windowName.c_str(), nodeId);
        }
    }

    // レイアウト構築完了
    ImGui::DockBuilderFinish(dockMain);
}

void DockingUI::LoadPlaybackIcons()
{
    auto& texManager = TextureManager::GetInstance();

    // テクスチャマネージャーが初期化されているか確認
    if (!texManager.IsInitialized()) {
        return;
    }

    try {
        // 再生制御アイコンを読み込む
        auto playTex = texManager.Load("reproduction.png");
        auto pauseTex = texManager.Load("pause.png");
        auto gridTex = texManager.Load("grid.png");

        // GPUハンドルを保存
        playIcon_ = playTex.gpuHandle;
        pauseIcon_ = pauseTex.gpuHandle;
        gridIcon_ = gridTex.gpuHandle;

        playbackIconsLoaded_ = true;
        Logger::GetInstance().Log("Playback icons loaded successfully (DockingUI)", LogLevel::INFO, LogCategory::Graphics);
    }
    catch (const std::exception& e) {
        std::string errorMsg = std::format("Failed to load playback icons: {}", e.what());
        Logger::GetInstance().Log(errorMsg, LogLevel::WARNING, LogCategory::Graphics);
        playbackIconsLoaded_ = false;
    }
}

void DockingUI::DrawPlaybackToolbar()
{
    // アイコンがまだ読み込まれていない場合は読み込む
    if (!playbackIconsLoaded_) {
        LoadPlaybackIcons();
    }

    ImGuiViewport* vp = ImGui::GetMainViewport();
    float menuBarHeight = ImGui::GetFrameHeight();

    // ツールバーの位置とサイズを設定
    ImVec2 toolbarPos = ImVec2(vp->Pos.x, vp->Pos.y + menuBarHeight);
    ImVec2 toolbarSize = ImVec2(vp->Size.x, toolbarHeight_);

    ImGui::SetNextWindowPos(toolbarPos);
    ImGui::SetNextWindowSize(toolbarSize);

    ImGuiWindowFlags toolbarFlags = ImGuiWindowFlags_NoTitleBar
        | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse
        | ImGuiWindowFlags_NoDocking
        | ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));

    // ポーズ中は背景色を少し暗くする
    auto& playbackManager = PlaybackStateManager::GetInstance();
    if (playbackManager.IsPaused()) {
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));  // 少し暗め
    } else {
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));  // 通常
    }

    if (ImGui::Begin("##PlaybackToolbar", nullptr, toolbarFlags)) {

        if (!playbackIconsLoaded_) {
            // アイコンが読み込まれていない場合はテキストボタンを表示
            PlaybackState currentState = playbackManager.GetState();

            // 中央に配置（2ボタンのみ）
            float buttonWidth = 50.0f;
            float spacing = 8.0f;
            float totalWidth = buttonWidth * 2 + spacing;
            float startX = (vp->Size.x - totalWidth) * 0.5f;

            ImGui::SetCursorPosX(startX);

            if (currentState == PlaybackState::Playing) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.26f, 0.59f, 0.98f, 1.00f));
            }
            if (ImGui::Button("Play", ImVec2(buttonWidth, 22.0f))) {
                playbackManager.Play();
            }
            if (currentState == PlaybackState::Playing) {
                ImGui::PopStyleColor();
            }

            ImGui::SameLine(0, spacing);

            if (currentState == PlaybackState::Paused) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.26f, 0.59f, 0.98f, 1.00f));
            }
            if (ImGui::Button("Pause", ImVec2(buttonWidth, 22.0f))) {
                playbackManager.Pause();
            }
            if (currentState == PlaybackState::Paused) {
                ImGui::PopStyleColor();
            }

        } else {
            // アイコンボタンを表示（再生/ポーズ + グリッド）
            PlaybackState currentState = playbackManager.GetState();

            constexpr float kIconSize = 18.0f;  // アイコンサイズを小さく
            constexpr float kPadding = 3.0f;
            constexpr float kSpacing = 6.0f;
            constexpr float kGroupSpacing = 16.0f;  // グループ間のスペース（再生ボタンとグリッドの間）
            const float buttonSize = kIconSize + kPadding * 2.0f;
            const float playbackGroupWidth = buttonSize * 2 + kSpacing;  // 再生/ポーズボタン
            const float gridButtonWidth = buttonSize;  // グリッドボタン
            const float totalWidth = playbackGroupWidth + kGroupSpacing + gridButtonWidth;
            const float startX = (vp->Size.x - totalWidth) * 0.5f;

            ImGui::SetCursorPosX(startX);

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(kPadding, kPadding));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);

            // 再生ボタン
            {
                bool isActive = (currentState == PlaybackState::Playing);
                if (isActive) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.26f, 0.59f, 0.98f, 1.00f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.36f, 0.69f, 1.00f, 1.00f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.16f, 0.49f, 0.88f, 1.00f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.22f, 0.22f, 1.00f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.35f, 0.35f, 1.00f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.15f, 0.15f, 1.00f));
                }

                if (ImGui::ImageButton("##PlayBtn", (ImTextureID)playIcon_.ptr, ImVec2(kIconSize, kIconSize))) {
                    playbackManager.Play();
                }
                ImGui::PopStyleColor(3);

                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                    ImGui::SetTooltip("再生 (Play)");
                }
            }

            ImGui::SameLine(0, kSpacing);

            // 一時停止ボタン
            {
                bool isActive = (currentState == PlaybackState::Paused);
                if (isActive) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.26f, 0.59f, 0.98f, 1.00f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.36f, 0.69f, 1.00f, 1.00f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.16f, 0.49f, 0.88f, 1.00f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.22f, 0.22f, 1.00f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.35f, 0.35f, 1.00f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.15f, 0.15f, 1.00f));
                }

                if (ImGui::ImageButton("##PauseBtn", (ImTextureID)pauseIcon_.ptr, ImVec2(kIconSize, kIconSize))) {
                    playbackManager.Pause();
                }
                ImGui::PopStyleColor(3);

                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                    ImGui::SetTooltip("一時停止 (Pause)");
                }
            }

            // グループ間のスペーシング
            ImGui::SameLine(0, kGroupSpacing);

            // グリッドボタン
            {
                bool isActive = isGridVisible_;
                if (isActive) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.26f, 0.59f, 0.98f, 1.00f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.36f, 0.69f, 1.00f, 1.00f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.16f, 0.49f, 0.88f, 1.00f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.22f, 0.22f, 1.00f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.35f, 0.35f, 1.00f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.15f, 0.15f, 1.00f));
                }

                if (ImGui::ImageButton("##GridBtn", (ImTextureID)gridIcon_.ptr, ImVec2(kIconSize, kIconSize))) {
                    isGridVisible_ = !isGridVisible_;
                }
                ImGui::PopStyleColor(3);

                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                    ImGui::SetTooltip(isGridVisible_ ? "グリッドを非表示 (Hide Grid)" : "グリッドを表示 (Show Grid)");
                }
            }

            ImGui::PopStyleVar(2);
        }
    }
    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}
}
