#include "pch.h"
#include "DockingUI.h"
#include "Editor/Scene/SceneDebugEditor.h"
#include "Graphics/Texture/TextureManager.h"
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
        if (windowName == "Game" || windowName == "Canvas") {
            return gameNodeId_;
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

        gameNodeId_ = idCenter;

        nodeIds_[static_cast<int>(DockArea::LeftTop)] = idLeftTop;
        nodeIds_[static_cast<int>(DockArea::LeftBottom)] = idLeftBottom;
        nodeIds_[static_cast<int>(DockArea::Center)] = idCenter;
        nodeIds_[static_cast<int>(DockArea::Right)] = idRight;
        nodeIds_[static_cast<int>(DockArea::BottomLeft)] = 0;
        nodeIds_[static_cast<int>(DockArea::BottomRight)] = 0;
        nodeIds_[static_cast<int>(DockArea::Bottom)] = idBottom;
        nodeIds_[static_cast<int>(DockArea::Hierarchy)] = idLeftTop;

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
            // 全アイコンを並列ロードに投入する。
            std::vector<std::string> iconPaths = {
                "grid.png", "translate.png", "rotate.png", "scale.png",
                "fps.png", "deltaTime.png"
            };
            texManager.Load(iconPaths);

            // キャッシュ済みの結果を割り当てる。
            auto gridTex = texManager.Load("grid.png");
            auto translateTex = texManager.Load("translate.png");
            auto rotateTex = texManager.Load("rotate.png");
            auto scaleTex = texManager.Load("scale.png");

            gridIcon_ = gridTex.gpuHandle;
            translateIcon_ = translateTex.gpuHandle;
            rotateIcon_ = rotateTex.gpuHandle;
            scaleIcon_ = scaleTex.gpuHandle;

            // ステータスバー用アイコン
            auto fpsTex = texManager.Load("fps.png");
            fpsIcon_ = fpsTex.gpuHandle;
            fpsIconLoaded_ = true;

            auto deltaTimeTex = texManager.Load("deltaTime.png");
            deltaTimeIcon_ = deltaTimeTex.gpuHandle;
            deltaTimeIconLoaded_ = true;

            playbackIconsLoaded_ = true;
            Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Graphics, "{}", "Toolbar icons loaded successfully (DockingUI)");
        }
        catch (const std::exception& e) {
            std::string errorMsg = std::format("Failed to load toolbar icons: {}", e.what());
            Logger::GetInstance().Logf(LogLevel::WARNING, LogCategory::Graphics, "{}", errorMsg);
            playbackIconsLoaded_ = false;
        }
    }

    void DockingUI::DrawPlaybackToolbar()
    {
#ifdef USE_IMGUI
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

        if (auto toolbar = UI::Scope::WindowScope("##PlaybackToolbar", nullptr, toolbarFlags)) {
            if (playbackIconsLoaded_) {
                constexpr float kIconSize = 18.0f;
                constexpr float kPadding = 3.0f;

                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(kPadding, kPadding));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
                ImGui::SetCursorPosX(8.0f);

                if (sceneDebugEditor_) {
                    const auto drawGizmoButton = [&](const char* id, D3D12_GPU_DESCRIPTOR_HANDLE icon, Gizmo::Mode mode, const char* tooltip) {
                        const bool isActive = (sceneDebugEditor_->GetGizmoMode() == mode);
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
                            sceneDebugEditor_->SetGizmoMode(mode);
                        }
                        ImGui::PopStyleColor(3);
                        UI::Tooltip(tooltip);
                        };

                    drawGizmoButton("##GizmoTranslateToolbar", translateIcon_, Gizmo::Mode::Translate, "移動 [W]");
                    UI::SameLine(0.0f, 6.0f);
                    drawGizmoButton("##GizmoRotateToolbar", rotateIcon_, Gizmo::Mode::Rotate, "回転 [E]");
                    UI::SameLine(0.0f, 6.0f);
                    drawGizmoButton("##GizmoScaleToolbar", scaleIcon_, Gizmo::Mode::Scale, "拡縮 [R]");
                    UI::SameLine(0.0f, 16.0f);
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

                    UI::Tooltip(isGridVisible_ ? "グリッドを非表示 (Hide Grid)" : "グリッドを表示 (Show Grid)");
                }

                ImGui::PopStyleVar(2);
            }
        }

        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
#endif
    }

    void DockingUI::DrawStatusBar([[maybe_unused]] float fps, [[maybe_unused]] float deltaTimeMs)
    {
#ifdef USE_IMGUI
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

        if (auto statusBar = UI::Scope::WindowScope("##StatusBar", nullptr, flags)) {
            constexpr float kFpsIconSize = 16.0f;
            constexpr float kDeltaTimeIconSize = 32.0f;
            constexpr float kSeparatorSpacing = 16.0f;
            const float windowHeight = ImGui::GetWindowHeight();
            const float textCenterY = (windowHeight - ImGui::GetTextLineHeight()) * 0.5f;

            // FPS アイコン
            if (fpsIconLoaded_) {
                ImGui::SetCursorPosY((windowHeight - kFpsIconSize) * 0.5f);
                ImGui::Image((ImTextureID)fpsIcon_.ptr, ImVec2(kFpsIconSize, kFpsIconSize));
                UI::SameLine(0.0f, 6.0f);
            }

            // FPS テキスト
            ImGui::SetCursorPosY(textCenterY);
            ImGui::Text("[frame per second]: %.1f fps", fps);

            // セパレーター
            UI::SameLine(0.0f, kSeparatorSpacing);
            ImGui::SetCursorPosY(textCenterY);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
            UI::Label("|");
            ImGui::PopStyleColor();

            // デルタタイムアイコン
            UI::SameLine(0.0f, kSeparatorSpacing);
            if (deltaTimeIconLoaded_) {
                ImGui::SetCursorPosY((windowHeight - kDeltaTimeIconSize) * 0.5f);
                ImGui::Image((ImTextureID)deltaTimeIcon_.ptr, ImVec2(kDeltaTimeIconSize, kDeltaTimeIconSize));
                UI::SameLine(0.0f, 6.0f);
            }

            // デルタタイムテキスト
            ImGui::SetCursorPosY(textCenterY);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.85f, 1.0f, 1.0f));
            ImGui::Text("[exec speed / frame]: %.4f sec", deltaTimeMs * 0.001f);
            ImGui::PopStyleColor();

            // ── ホバー時に CPU / GPU タイムスタンプを表示 ─────────
            const bool hasTimingData = timingData_[static_cast<uint32_t>(GpuTimestampSlot::Total)].gpuMs > 0.0f
                || timingData_[static_cast<uint32_t>(GpuTimestampSlot::Total)].cpuMs > 0.0f;
            if (ImGui::IsWindowHovered() && hasTimingData)
            {
                if (auto tooltip = UI::Scope::TooltipScope())
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Frame Timing");
                    UI::SameLine();
                    UI::Hint("(1-frame delay)");
                    UI::Separator();
                    UI::Spacing();

                    // 1フレームバジェット（60fps = 16.67ms）を基準にバーを描画
                    constexpr float kFrameBudgetMs = 1000.0f / 60.0f;

                    constexpr ImGuiTableFlags tableFlags =
                        ImGuiTableFlags_BordersInnerV
                        | ImGuiTableFlags_BordersOuter
                        | ImGuiTableFlags_RowBg;

                    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8.0f, 4.0f));
                    if (auto table = UI::Scope::TableScope("##timing_table", 4, tableFlags))
                    {
                        ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableSetupColumn("CPU (ms)", ImGuiTableColumnFlags_WidthFixed, 68.0f);
                        ImGui::TableSetupColumn("GPU (ms)", ImGuiTableColumnFlags_WidthFixed, 68.0f);
                        ImGui::TableSetupColumn("Budget", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                        ImGui::TableHeadersRow();

                        // パス名から解決したカテゴリ（GpuTimingCategory）でグルーピングする。
                        // RenderGraph に新規パスを追加してもこのテーブルは編集不要（BuildGpuTimingGroups が自動集計）。
                        const std::vector<GpuTimingGroup> groups = BuildGpuTimingGroups(timingData_);

                        // Total（Frame カテゴリ）スロットのインデックスを探す
                        uint32_t totalIdx = static_cast<uint32_t>(timingData_.size());
                        for (uint32_t i = 0; i < timingData_.size(); ++i)
                        {
                            if (timingData_[i].category == GpuTimingCategory::Frame) { totalIdx = i; break; }
                        }

                        for (const auto& group : groups)
                        {
                            // カテゴリヘッダー行
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s",
                                GpuTimestampProfiler::GetCategoryLabel(group.category));

                            for (uint32_t idx : group.slotIndices)
                            {
                                if (idx >= timingData_.size()) continue;
                                const auto& slot = timingData_[idx];

                                ImGui::TableNextRow();

                                // 今フレーム実行されなかったパスも行位置を保つ（行の出入りで
                                // 下の全パスがずれると内訳が読めなくなる）。淡色で区別する。
                                const bool idle = IsIdleTimingSlot(slot);
                                const ImVec4 kIdle = ImVec4(0.45f, 0.45f, 0.45f, 1.0f);

                                const ImVec4 gpuColor = idle ? kIdle
                                    : (slot.gpuMs > 8.0f) ? ImVec4(1.0f, 0.35f, 0.35f, 1.0f)
                                    : (slot.gpuMs > 4.0f) ? ImVec4(1.0f, 0.80f, 0.20f, 1.0f)
                                    : ImVec4(0.45f, 0.90f, 0.45f, 1.0f);

                                ImGui::TableSetColumnIndex(0);
                                {
                                    const ImVec2 dotPos = {
                                        ImGui::GetCursorScreenPos().x + 4.0f,
                                        ImGui::GetCursorScreenPos().y + ImGui::GetTextLineHeight() * 0.5f
                                    };
                                    ImGui::GetWindowDrawList()->AddCircleFilled(
                                        dotPos, 4.0f,
                                        ImGui::ColorConvertFloat4ToU32(gpuColor));
                                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 14.0f);
                                }
                                UI::Label(slot.name);

                                ImGui::TableSetColumnIndex(1);
                                {
                                    const ImVec4 cpuColor = idle ? kIdle
                                        : (slot.cpuMs > 2.0f) ? ImVec4(1.0f, 0.35f, 0.35f, 1.0f)
                                        : (slot.cpuMs > 0.5f) ? ImVec4(1.0f, 0.80f, 0.20f, 1.0f)
                                        : ImVec4(0.75f, 0.75f, 0.75f, 1.0f);
                                    ImGui::TextColored(cpuColor, "%.3f", slot.cpuMs);
                                }

                                ImGui::TableSetColumnIndex(2);
                                ImGui::TextColored(gpuColor, "%.3f", slot.gpuMs);

                                ImGui::TableSetColumnIndex(3);
                                {
                                    const float ratio =
                                        slot.gpuMs / kFrameBudgetMs < 1.0f
                                        ? slot.gpuMs / kFrameBudgetMs : 1.0f;
                                    const ImVec4 barColor =
                                        (slot.gpuMs > 8.0f) ? ImVec4(0.9f, 0.25f, 0.25f, 1.0f)
                                        : (slot.gpuMs > 4.0f) ? ImVec4(0.9f, 0.70f, 0.10f, 1.0f)
                                        : ImVec4(0.20f, 0.75f, 0.30f, 1.0f);
                                    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barColor);
                                    const auto overlay = std::format("{:.1f}%", ratio * 100.0f);
                                    UI::ProgressBar(ratio,
                                        ImVec2(-FLT_MIN, ImGui::GetTextLineHeight()), overlay.c_str());
                                    ImGui::PopStyleColor();
                                }
                            }
                        }

                        // Frame Total 行
                        if (totalIdx < timingData_.size())
                        {
                            const auto& slot = timingData_[totalIdx];
                            ImGui::TableNextRow();
                            constexpr ImU32 kTotalBg = IM_COL32(40, 40, 48, 255);
                            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, kTotalBg);
                            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, kTotalBg);

                            const ImVec4 gpuColor =
                                (slot.gpuMs > 8.0f) ? ImVec4(1.0f, 0.35f, 0.35f, 1.0f)
                                : (slot.gpuMs > 4.0f) ? ImVec4(1.0f, 0.80f, 0.20f, 1.0f)
                                : ImVec4(0.45f, 0.90f, 0.45f, 1.0f);

                            ImGui::TableSetColumnIndex(0);
                            {
                                const ImVec2 dotPos = {
                                    ImGui::GetCursorScreenPos().x + 4.0f,
                                    ImGui::GetCursorScreenPos().y + ImGui::GetTextLineHeight() * 0.5f
                                };
                                ImGui::GetWindowDrawList()->AddCircleFilled(
                                    dotPos, 4.0f,
                                    ImGui::ColorConvertFloat4ToU32(gpuColor));
                                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 14.0f);
                            }
                            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s", slot.name);

                            ImGui::TableSetColumnIndex(1);
                            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%.3f", slot.cpuMs);

                            ImGui::TableSetColumnIndex(2);
                            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%.3f", slot.gpuMs);

                            ImGui::TableSetColumnIndex(3);
                            {
                                const float ratio =
                                    slot.gpuMs / kFrameBudgetMs < 1.0f
                                    ? slot.gpuMs / kFrameBudgetMs : 1.0f;
                                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.9f, 0.70f, 0.10f, 1.0f));
                                const auto overlay = std::format("{:.1f}%", ratio * 100.0f);
                                UI::ProgressBar(ratio,
                                    ImVec2(-FLT_MIN, ImGui::GetTextLineHeight()), overlay.c_str());
                                ImGui::PopStyleColor();
                            }
                        }
                    }
                    ImGui::PopStyleVar();
                }
            }
        }

        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
#endif
    }
}


