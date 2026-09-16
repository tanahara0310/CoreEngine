#include "pch.h"
#include "DockingUI.h"
#include "Editor/ImGui/EditorTheme.h"
#include "Editor/ImGui/Widgets/EditorBars.h"
#include "Editor/Scene/SceneDebugEditor.h"
#include "EngineSystem/PlaybackState.h"
#include "Utility/CVar/CVar.h"
#include "Utility/CVar/CVarConsole.h"
#include "Utility/CVar/CVarRegistry.h"
#include <algorithm>
#include <format>


namespace CoreEngine
{
    namespace
    {
        namespace Theme = Editor::Theme;

        /// 標準レイアウトの区画の割合（画面全体に対する幅・高さ）
        constexpr float kRightRatio = 0.27f;   // Inspector
        constexpr float kLeftRatio = 0.22f;   // Hierarchy / Project の列
        constexpr float kBottomRatio = 0.28f;   // Console / Profiler（中央列の高さに対して）
        constexpr float kProjectRatio = 0.40f;   // Project（左列の高さに対して）

        /// コライダー表示の切り替え先
        constexpr const char* kColliderCVar = "r.Collision.DebugDraw";

        /// @brief bool の CVar を Undo に積んで書き換える
        void SetBoolCVar(const char* name, bool value)
        {
            ICVar* const cvar = CVarRegistry::Get().Find(name);
            if (!cvar) {
                return;
            }
            std::string reason;
            CVarConsole::SetFromString(*cvar, value ? "true" : "false", reason);
        }

        /// @brief bool の CVar の現在値（無ければ false）
        bool GetBoolCVar(const char* name)
        {
            const ICVar* const cvar = CVarRegistry::Get().Find(name);
            const bool* const value = cvar ? cvar->AsBool() : nullptr;
            return value && *value;
        }

        /// @brief 残りの幅から右詰めで描き始める位置へ移動する
        void AlignRight(float width)
        {
            ImGui::SameLine(0.0f, 0.0f);
            const float x = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - width;
            if (x > ImGui::GetCursorPosX()) {
                ImGui::SetCursorPosX(x);
            }
        }
    }

    DockSpaceHostScope::~DockSpaceHostScope()
    {
        ImGui::End();
    }

    void DockingUI::RegisterWindow(const std::string& windowName, Editor::DockArea area)
    {
        if (area == Editor::DockArea::None) {
            return;
        }

        const auto found = std::find_if(registeredWindows_.begin(), registeredWindows_.end(),
            [&windowName](const auto& entry) { return entry.first == windowName; });
        if (found != registeredWindows_.end()) {
            found->second = area;
        } else {
            registeredWindows_.emplace_back(windowName, area);
        }

        // レイアウトが既に初期化されている場合、動的にドッキング
        if (layoutInitialized_) {
            const ImGuiID nodeId = GetNodeIdForArea(area);
            if (nodeId != 0) {
                ImGui::DockBuilderDockWindow(windowName.c_str(), nodeId);
            }
        }
    }

    void DockingUI::UnregisterWindow(const std::string& windowName)
    {
        std::erase_if(registeredWindows_,
            [&windowName](const auto& entry) { return entry.first == windowName; });
    }

    DockSpaceHostScope DockingUI::BeginDockSpaceHost()
    {
        // メインビューポートに合わせてホストウィンドウの位置・サイズを設定
        ImGuiViewport* vp = ImGui::GetMainViewport();

        // メニューバーの高さを取得
        const float menuBarHeight = ImGui::GetFrameHeight();

        // 再生ツールバーを描画（メニューバーの直下）
        DrawPlaybackToolbar();

        // メニューバー + ツールバーの下にドッキングエリアを配置
        const float totalTopHeight = menuBarHeight + toolbarHeight_;
        ImVec2 pos = vp->Pos;
        pos.y += totalTopHeight;

        ImVec2 size = vp->Size;
        size.y -= totalTopHeight + statusBarHeight_;

        ImGui::SetNextWindowPos(pos);
        ImGui::SetNextWindowSize(size);
        ImGui::SetNextWindowViewport(vp->ID);

        // タイトルバーや移動不可など、ドッキング用の特殊フラグを設定
        const ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoTitleBar
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

        // レイアウト構築は必ず DockSpace() の提出より前に行うこと。
        // 提出後に DockBuilderRemoveNode すると、そのフレームのドックスペースと
        // ノードの対応が失われ、ドック中の全ウィンドウが表示されなくなる。
        SetupDockSpace();

        // ドッキングスペースを作成（中央透過）
        const ImGuiID dockId = ImGui::GetID("MyDockSpace");
        ImGui::DockSpace(dockId, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);

        return DockSpaceHostScope{};
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

    ImGuiID DockingUI::GetNodeIdForArea(Editor::DockArea area) const
    {
        return nodeIds_[static_cast<int>(area)];
    }

    void DockingUI::BuildDockLayout()
    {
        // ルートノード作成＆リセット
        ImGuiViewport* vp = ImGui::GetMainViewport();

        // メニューバー + ツールバーの高さを考慮したサイズを設定
        const float menuBarHeight = ImGui::GetFrameHeight();
        const float totalTopHeight = menuBarHeight + toolbarHeight_;
        const ImVec2 dockSpaceSize = ImVec2(vp->Size.x, vp->Size.y - totalTopHeight - statusBarHeight_);

        const ImGuiID dockMain = ImGui::GetID("MyDockSpace");
        ImGui::DockBuilderRemoveNode(dockMain);
        // ホストウィンドウ内に埋め込むノードなので DockSpace フラグを立てる。
        // 立てないと「自前のウィンドウを持つ浮遊ノード」として作られ、
        // 直後の DockSpace() と種別が食い違う。
        ImGui::DockBuilderAddNode(dockMain, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockMain, dockSpaceSize);

        for (ImGuiID& nodeId : nodeIds_) {
            nodeId = 0;
        }

        // 左右の列は画面の高さいっぱいに取り、中央の列だけを上下に割る。
        // 分割の割合は「今から割るノード」に対する比なので、画面全体に対する
        // 割合から、既に切り出した分を割り戻す。
        ImGuiID idRight = 0, idWithoutRight = 0;
        ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, kRightRatio, &idRight, &idWithoutRight);

        ImGuiID idLeft = 0, idCenterColumn = 0;
        ImGui::DockBuilderSplitNode(idWithoutRight, ImGuiDir_Left,
            kLeftRatio / (1.0f - kRightRatio), &idLeft, &idCenterColumn);

        ImGuiID idBottom = 0, idCenter = 0;
        ImGui::DockBuilderSplitNode(idCenterColumn, ImGuiDir_Down, kBottomRatio, &idBottom, &idCenter);

        ImGuiID idLeftBottom = 0, idLeftTop = 0;
        ImGui::DockBuilderSplitNode(idLeft, ImGuiDir_Down, kProjectRatio, &idLeftBottom, &idLeftTop);

        nodeIds_[static_cast<int>(Editor::DockArea::None)] = 0;
        nodeIds_[static_cast<int>(Editor::DockArea::LeftTop)] = idLeftTop;
        nodeIds_[static_cast<int>(Editor::DockArea::LeftBottom)] = idLeftBottom;
        nodeIds_[static_cast<int>(Editor::DockArea::Center)] = idCenter;
        nodeIds_[static_cast<int>(Editor::DockArea::Right)] = idRight;
        nodeIds_[static_cast<int>(Editor::DockArea::Bottom)] = idBottom;

        // 登録されているウィンドウを各ノードへ必ずドッキングする。
        // 【この無条件ドックを条件付きにしないこと】imgui.ini の DockId 欠落は
        // 「ユーザーが意図して引き出した」ことを意味しないため、判定を入れると
        // Hierarchy や Console が毎起動フローティングのまま復帰しなくなる。
        for (const auto& [windowName, area] : registeredWindows_) {
            const ImGuiID nodeId = GetNodeIdForArea(area);
            if (nodeId != 0) {
                ImGui::DockBuilderDockWindow(windowName.c_str(), nodeId);
            }
        }

        // レイアウト構築完了
        ImGui::DockBuilderFinish(dockMain);
    }

    void DockingUI::DrawPlaybackToolbar()
    {
#ifdef USE_IMGUI
        ImGuiViewport* vp = ImGui::GetMainViewport();
        const float menuBarHeight = ImGui::GetFrameHeight();

        ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + menuBarHeight));
        ImGui::SetNextWindowSize(ImVec2(vp->Size.x, toolbarHeight_));
        ImGui::SetNextWindowViewport(vp->ID);

        const ImGuiWindowFlags toolbarFlags = ImGuiWindowFlags_NoTitleBar
            | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoScrollbar
            | ImGuiWindowFlags_NoScrollWithMouse
            | ImGuiWindowFlags_NoDocking
            | ImGuiWindowFlags_NoSavedSettings;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(9.0f, 4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5.0f, 4.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::kWindow);

        if (auto toolbar = UI::Scope::WindowScope("##PlaybackToolbar", nullptr, toolbarFlags)) {
            DrawPlaybackButtons();
            UI::Bar::Separator();
            DrawGizmoButtons();
            UI::Bar::Separator();
            DrawViewToggles();
            DrawToolbarStatusChips();
        }

        ImGui::PopStyleColor();
        ImGui::PopStyleVar(4);
#endif
}

    void DockingUI::DrawPlaybackButtons()
    {
#ifdef USE_IMGUI
        auto& playback = PlaybackStateManager::GetInstance();
        const bool playing = playback.IsPlaying();

        if (UI::Bar::TransportButton("##Play", UI::Bar::Transport::Play, playing,
            "再生 [Ctrl+P]\nゲームの更新を進めます")) {
            playback.Play();
        }

        ImGui::SameLine();
        if (UI::Bar::TransportButton("##Pause", UI::Bar::Transport::Pause, !playing,
            "一時停止 [Ctrl+Shift+P]\nゲームの更新だけを止めます。\n"
            "止めている間もカメラ・ギズモ・各パネルは動きます")) {
            playback.Stop();
        }

        ImGui::SameLine();
        if (UI::Bar::TransportButton("##Step", UI::Bar::Transport::Step, false,
            "コマ送り\n止めたまま 1 フレームだけ進めます", !playing)) {
            playback.RequestStep();
        }
#endif
}

    void DockingUI::DrawGizmoButtons()
    {
#ifdef USE_IMGUI
        if (!sceneDebugEditor_) {
            return;
        }

        const auto modeButton = [this](const char* label, Gizmo::Mode mode, const char* tooltip) {
            if (UI::Bar::Button(label, sceneDebugEditor_->GetGizmoMode() == mode, tooltip)) {
                sceneDebugEditor_->SetGizmoMode(mode);
            }
            };

        modeButton("✥ 移動", Gizmo::Mode::Translate, "移動 [W]");
        ImGui::SameLine();
        modeButton("⟳ 回転", Gizmo::Mode::Rotate, "回転 [E]");
        ImGui::SameLine();
        modeButton("⤢ 拡縮", Gizmo::Mode::Scale, "拡縮 [R]");
#endif
}

    void DockingUI::DrawViewToggles()
    {
#ifdef USE_IMGUI
        if (UI::Bar::Button("▦ Grid", isGridVisible_,
            isGridVisible_ ? "グリッドを隠す" : "グリッドを出す")) {
            isGridVisible_ = !isGridVisible_;
        }

        ImGui::SameLine();
        const bool collider = GetBoolCVar(kColliderCVar);
        if (UI::Bar::Button("◍ Collider", collider,
            "当たり判定の形を描く（r.Collision.DebugDraw）")) {
            SetBoolCVar(kColliderCVar, !collider);
        }
#endif
}

    void DockingUI::DrawToolbarStatusChips()
    {
#ifdef USE_IMGUI
        const std::string scriptText = status_.scriptOk
            ? std::format("Script ✓ {} 型", status_.scriptTypeCount)
            : std::string("Script ✕ コンパイル失敗");
        const char* const saveText = status_.sceneSaved ? "保存済み" : "未保存の変更";

        const float width = UI::Bar::ChipWidth(scriptText.c_str())
            + ImGui::GetStyle().ItemSpacing.x + UI::Bar::ChipWidth(saveText);
        AlignRight(width);

        UI::Bar::Chip(scriptText.c_str(),
            status_.scriptOk ? Theme::kOk : Theme::kError,
            Theme::WithAlpha(status_.scriptOk ? Theme::kOk : Theme::kError, 0.35f));

        ImGui::SameLine();
        UI::Bar::Chip(saveText,
            status_.sceneSaved ? Theme::kTextMute : Theme::kWarm,
            status_.sceneSaved ? Theme::kOutline : Theme::WithAlpha(Theme::kWarm, 0.35f));
#endif
}

    void DockingUI::DrawStatusBar()
    {
#ifdef USE_IMGUI
        ImGuiViewport* vp = ImGui::GetMainViewport();
        const ImVec2 pos = ImVec2(vp->Pos.x, vp->Pos.y + vp->Size.y - statusBarHeight_);
        const ImVec2 size = ImVec2(vp->Size.x, statusBarHeight_);

        ImGui::SetNextWindowPos(pos);
        ImGui::SetNextWindowSize(size);
        ImGui::SetNextWindowViewport(vp->ID);

        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
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
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::kPanel);

        // フレームレートは数値の色だけで状態を示す（帯を状態色で塗らない）
        constexpr float kTargetFPS = 60.0f;
        const ImVec4 fpsColor =
            (status_.fps >= kTargetFPS * 0.90f) ? Theme::kOk
            : (status_.fps >= kTargetFPS * 0.80f) ? Theme::kWarn
            : Theme::kError;

        if (auto statusBar = UI::Scope::WindowScope("##StatusBar", nullptr, flags)) {
            const float textCenterY = (ImGui::GetWindowHeight() - ImGui::GetTextLineHeight()) * 0.5f;
            const auto item = [textCenterY](const ImVec4& color, const std::string& text) {
                ImGui::SetCursorPosY(textCenterY);
                ImGui::TextColored(color, "%s", text.c_str());
                };

            const auto& total = timingData_[static_cast<uint32_t>(GpuTimestampSlot::Total)];

            item(fpsColor, std::format("{:.1f} FPS", status_.fps));
            ImGui::SameLine(0.0f, 14.0f);
            item(Theme::kTextMute, std::format("CPU {:.1f}ms", total.cpuMs));
            ImGui::SameLine(0.0f, 14.0f);
            item(Theme::kTextMute, std::format("GPU {:.1f}ms", total.gpuMs));

            // ── 右側：スクリプト・Undo・シーンの保存状態 ──
            const std::string scriptText = status_.scriptOk ? "Script OK" : "Script 失敗";
            const std::string undoText = std::format("Undo {}", status_.undoCount);
            const std::string sceneText = status_.sceneName.empty()
                ? std::string("シーンなし")
                : status_.sceneName + (status_.sceneSaved ? " · 保存済み" : " · 未保存の変更");

            const float width = ImGui::CalcTextSize(scriptText.c_str()).x
                + ImGui::CalcTextSize(undoText.c_str()).x
                + ImGui::CalcTextSize(sceneText.c_str()).x + 28.0f;
            AlignRight(width);

            item(status_.scriptOk ? Theme::kOk : Theme::kError, scriptText);
            ImGui::SameLine(0.0f, 14.0f);
            item(Theme::kTextMute, undoText);
            ImGui::SameLine(0.0f, 14.0f);
            item(status_.sceneSaved ? Theme::kTextMute : Theme::kWarm, sceneText);

            DrawTimingTooltip();
        }

        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
#endif
}

    void DockingUI::DrawTimingTooltip()
    {
#ifdef USE_IMGUI
        const bool hasTimingData = timingData_[static_cast<uint32_t>(GpuTimestampSlot::Total)].gpuMs > 0.0f
            || timingData_[static_cast<uint32_t>(GpuTimestampSlot::Total)].cpuMs > 0.0f;
        if (!ImGui::IsWindowHovered() || !hasTimingData) {
            return;
        }

        auto tooltip = UI::Scope::TooltipScope();
        if (!tooltip) {
            return;
        }

        ImGui::TextColored(Theme::kWarm, "Frame Timing");
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

            // 1 行分のセル（名前の左に状態の丸、右端にバジェット比のバー）
            const auto drawRow = [](const GpuTimingResult& slot, bool idle, bool emphasize) {
                const ImVec4 kIdle = ImVec4(0.45f, 0.45f, 0.45f, 1.0f);
                const ImVec4 gpuColor = emphasize ? Theme::kWarm
                    : idle ? kIdle
                    : (slot.gpuMs > 8.0f) ? Theme::kError
                    : (slot.gpuMs > 4.0f) ? Theme::kWarn
                    : Theme::kOk;

                ImGui::TableSetColumnIndex(0);
                {
                    const ImVec2 dotPos = {
                        ImGui::GetCursorScreenPos().x + 4.0f,
                        ImGui::GetCursorScreenPos().y + ImGui::GetTextLineHeight() * 0.5f
                    };
                    ImGui::GetWindowDrawList()->AddCircleFilled(dotPos, 4.0f,
                        ImGui::ColorConvertFloat4ToU32(gpuColor));
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 14.0f);
                }
                if (emphasize) {
                    ImGui::TextColored(Theme::kWarm, "%s", slot.name);
                } else {
                    UI::Label(slot.name);
                }

                ImGui::TableSetColumnIndex(1);
                {
                    const ImVec4 cpuColor = emphasize ? Theme::kWarm
                        : idle ? kIdle
                        : (slot.cpuMs > 2.0f) ? Theme::kError
                        : (slot.cpuMs > 0.5f) ? Theme::kWarn
                        : Theme::kTextDim;
                    ImGui::TextColored(cpuColor, "%.3f", slot.cpuMs);
                }

                ImGui::TableSetColumnIndex(2);
                ImGui::TextColored(gpuColor, "%.3f", slot.gpuMs);

                ImGui::TableSetColumnIndex(3);
                {
                    const float ratio =
                        slot.gpuMs / kFrameBudgetMs < 1.0f ? slot.gpuMs / kFrameBudgetMs : 1.0f;
                    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, gpuColor);
                    const auto overlay = std::format("{:.1f}%", ratio * 100.0f);
                    UI::ProgressBar(ratio, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight()), overlay.c_str());
                    ImGui::PopStyleColor();
                }
                };

            for (const auto& group : groups)
            {
                // カテゴリヘッダー行
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(Theme::kWarm, "%s",
                    GpuTimestampProfiler::GetCategoryLabel(group.category));

                for (uint32_t idx : group.slotIndices)
                {
                    if (idx >= timingData_.size()) continue;
                    ImGui::TableNextRow();

                    // 今フレーム実行されなかったパスも行位置を保つ（行の出入りで
                    // 下の全パスがずれると内訳が読めなくなる）。淡色で区別する。
                    drawRow(timingData_[idx], IsIdleTimingSlot(timingData_[idx]), false);
                }
            }

            // Frame Total 行
            if (totalIdx < timingData_.size())
            {
                ImGui::TableNextRow();
                constexpr ImU32 kTotalBg = IM_COL32(40, 40, 48, 255);
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, kTotalBg);
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, kTotalBg);
                drawRow(timingData_[totalIdx], false, true);
            }
        }
        ImGui::PopStyleVar();
#endif
}
}
