#include "DockingUI.h"
#include "Graphics/Texture/TextureManager.h"
#ifdef USE_IMGUI
#include "SceneViewport.h"
#endif
#include "ObjectSelector.h"
#include "Gizmo.h"
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
        size.y -= totalTopHeight + statusBarHeight_;

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
        if (layoutPreset_ == DockLayoutPreset::TwoByThree) {
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
        ImVec2 dockSpaceSize = ImVec2(vp->Size.x, vp->Size.y - totalTopHeight - statusBarHeight_);

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

        if (layoutPreset_ == DockLayoutPreset::TwoByThree) {
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
            nodeIds_[static_cast<int>(DockArea::Hierarchy)] = idToolColumn;
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
            nodeIds_[static_cast<int>(DockArea::Hierarchy)] = idLeftTop;
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

        if (!texManager.IsInitialized()) {
            return;
        }

        try {
            // ツールバーアイコンを読み込む
            auto gridTex = texManager.Load("grid.png");
            auto gizmoTranslateTex = texManager.Load("translate.png");
            auto gizmoRotateTex = texManager.Load("rotate.png");
            auto gizmoScaleTex = texManager.Load("scale.png");

            gridIcon_ = gridTex.gpuHandle;
            gizmoTranslateIcon_ = gizmoTranslateTex.gpuHandle;
            gizmoRotateIcon_ = gizmoRotateTex.gpuHandle;
            gizmoScaleIcon_ = gizmoScaleTex.gpuHandle;

            // ステータスバー用アイコン
            auto fpsTex = texManager.Load("fps.png");
            fpsIcon_ = fpsTex.gpuHandle;
            fpsIconLoaded_ = true;

            auto deltaTimeTex = texManager.Load("deltaTime.png");
            deltaTimeIcon_ = deltaTimeTex.gpuHandle;
            deltaTimeIconLoaded_ = true;

            playbackIconsLoaded_ = true;
            gizmoIconsLoaded_ = true;
            Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Graphics, "{}", "Toolbar icons loaded successfully (DockingUI)");
        }
        catch (const std::exception& e) {
            std::string errorMsg = std::format("Failed to load toolbar icons: {}", e.what());
            Logger::GetInstance().Logf(LogLevel::WARNING, LogCategory::Graphics, "{}", errorMsg);
            playbackIconsLoaded_ = false;
            gizmoIconsLoaded_ = false;
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
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));

        if (ImGui::Begin("##PlaybackToolbar", nullptr, toolbarFlags)) {
                    if (gizmoIconsLoaded_) {
            #ifdef USE_IMGUI
                        ObjectSelector* objectSelector = sceneViewport_ ? sceneViewport_->GetObjectSelector() : nullptr;
            #else
                        ObjectSelector* objectSelector = nullptr;
            #endif

                constexpr float kIconSize = 18.0f;
                constexpr float kPadding = 3.0f;
                constexpr float kSpacing = 6.0f;
                constexpr float kGroupSpacing = 16.0f;

                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(kPadding, kPadding));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);

                // Sceneビュー用ギズモ切替（左詰め）
                if (objectSelector) {
                    ImGui::SetCursorPosX(8.0f);

                    const auto drawGizmoButton = [&](const char* id, D3D12_GPU_DESCRIPTOR_HANDLE icon, Gizmo::Mode mode, const char* tooltip) {
                        const bool isActive = (objectSelector->GetGizmoMode() == mode);

                        if (isActive) {
                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.48f, 0.48f, 0.48f, 1.00f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.58f, 0.58f, 0.58f, 1.00f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.40f, 0.40f, 0.40f, 1.00f));
                        } else {
                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.22f, 0.22f, 0.55f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.35f, 0.35f, 0.85f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.15f, 0.15f, 0.90f));
                        }

                        if (ImGui::ImageButton(id, (ImTextureID)icon.ptr, ImVec2(kIconSize, kIconSize))) {
                            objectSelector->SetGizmoMode(mode);
                        }
                        ImGui::PopStyleColor(3);

                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                            ImGui::SetTooltip("%s", tooltip);
                        }
                        };

                    drawGizmoButton("##GizmoTranslateToolbar", gizmoTranslateIcon_, Gizmo::Mode::Translate, "移動 [W]");
                    ImGui::SameLine(0, kSpacing);
                    drawGizmoButton("##GizmoRotateToolbar", gizmoRotateIcon_, Gizmo::Mode::Rotate, "回転 [E]");
                    ImGui::SameLine(0, kSpacing);
                    drawGizmoButton("##GizmoScaleToolbar", gizmoScaleIcon_, Gizmo::Mode::Scale, "拡縮 [R]");

                    ImGui::SameLine(0, kGroupSpacing);
                } else {
                    ImGui::SetCursorPosX(8.0f);
                }

                // グリッドボタン
                {
                    bool isActive = isGridVisible_;
                    if (isActive) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.48f, 0.48f, 0.48f, 1.00f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.58f, 0.58f, 0.58f, 1.00f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.40f, 0.40f, 0.40f, 1.00f));
                    } else {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.22f, 0.22f, 0.55f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.35f, 0.35f, 0.85f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.15f, 0.15f, 0.90f));
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

    void DockingUI::DrawStatusBar(float fps, float deltaTimeMs)
    {
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImVec2 pos = ImVec2(vp->Pos.x, vp->Pos.y + vp->Size.y - statusBarHeight_);
        ImVec2 size = ImVec2(vp->Size.x, statusBarHeight_);

        ImGui::SetNextWindowPos(pos);
        ImGui::SetNextWindowSize(size);
        ImGui::SetNextWindowViewport(vp->ID);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
            | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoScrollbar
            | ImGuiWindowFlags_NoScrollWithMouse
            | ImGuiWindowFlags_NoDocking
            | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoFocusOnAppearing
            | ImGuiWindowFlags_NoNav;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 0.0f));

        // FPSに応じてステータスバー背景色を変化させる
        constexpr float kTargetFPS = 60.0f;
        ImVec4 barBgColor;
        if (fps >= kTargetFPS * 0.90f) {
            barBgColor = ImVec4(0.05f, 0.28f, 0.08f, 1.0f);  // 暗い緑
        } else if (fps >= kTargetFPS * 0.80f) {
            barBgColor = ImVec4(0.28f, 0.22f, 0.03f, 1.0f);  // 暗い黄
        } else {
            barBgColor = ImVec4(0.28f, 0.05f, 0.05f, 1.0f);  // 暗い赤
        }
        ImGui::PushStyleColor(ImGuiCol_WindowBg, barBgColor);

        if (ImGui::Begin("##StatusBar", nullptr, flags)) {
            constexpr float kFpsIconSize = 16.0f;
            constexpr float kDeltaTimeIconSize = 32.0f;
            constexpr float kSeparatorSpacing = 16.0f;
            const float windowHeight = ImGui::GetWindowHeight();
            const float textCenterY = (windowHeight - ImGui::GetTextLineHeight()) * 0.5f;

            // FPSアイコン
            if (fpsIconLoaded_) {
                ImGui::SetCursorPosY((windowHeight - kFpsIconSize) * 0.5f);
                ImGui::Image((ImTextureID)fpsIcon_.ptr, ImVec2(kFpsIconSize, kFpsIconSize));
                ImGui::SameLine(0, 6.0f);
            }

            // FPSテキスト（白固定）
            ImGui::SetCursorPosY(textCenterY);
            ImGui::Text("[frame per second]: %.1f fps", fps);

            // セパレーター
            ImGui::SameLine(0, kSeparatorSpacing);
            ImGui::SetCursorPosY(textCenterY);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
            ImGui::TextUnformatted("|");
            ImGui::PopStyleColor();

            // デルタタイムアイコン
            ImGui::SameLine(0, kSeparatorSpacing);
            if (deltaTimeIconLoaded_) {
                ImGui::SetCursorPosY((windowHeight - kDeltaTimeIconSize) * 0.5f);
                ImGui::Image((ImTextureID)deltaTimeIcon_.ptr, ImVec2(kDeltaTimeIconSize, kDeltaTimeIconSize));
                ImGui::SameLine(0, 6.0f);
            }

            // デルタタイムテキスト
            ImGui::SetCursorPosY(textCenterY);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.85f, 1.0f, 1.0f));
            ImGui::Text("[exec speed / frame]: %.4f sec", deltaTimeMs * 0.001f);
            ImGui::PopStyleColor();
        }
        ImGui::End();

        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
    }
}


