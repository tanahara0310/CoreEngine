#include "pch.h"
#include "RenderGraphEditorPanel.h"

#ifdef USE_IMGUI

#include <imnodes.h>

#include <algorithm>
#include <cstring>
#include <numeric>

#include "EngineSystem/EngineSystem.h"
#include "Graphics/RHI/Debug/GpuTimestampProfiler.h"
#include "Graphics/Render/Pass/RenderPass.h"
#include "Graphics/Render/Pass/RenderPipeline.h"
#include "Graphics/Render/RenderGraphSnapshot.h"

namespace CoreEngine
{
    namespace {

        /// @brief D3D12 リソース状態を短い表示名へ変換する
        const char* ResourceStateToShortString(D3D12_RESOURCE_STATES state)
        {
            switch (state) {
            case D3D12_RESOURCE_STATE_COMMON:                    return "COMMON";
            case D3D12_RESOURCE_STATE_RENDER_TARGET:             return "RENDER_TARGET";
            case D3D12_RESOURCE_STATE_UNORDERED_ACCESS:          return "UAV";
            case D3D12_RESOURCE_STATE_DEPTH_WRITE:               return "DEPTH_WRITE";
            case D3D12_RESOURCE_STATE_DEPTH_READ:                return "DEPTH_READ";
            case D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE:     return "PS_SRV";
            case D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE: return "NONPS_SRV";
            case D3D12_RESOURCE_STATE_COPY_SOURCE:               return "COPY_SRC";
            case D3D12_RESOURCE_STATE_COPY_DEST:                 return "COPY_DST";
            case D3D12_RESOURCE_STATE_GENERIC_READ:              return "GENERIC_READ";
            case D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE: return "RT_AS";
            default: break;
            }
            if (state == (D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)) {
                return "DEPTH_READ|PS_SRV";
            }
            return "MIXED";
        }

        /// @brief カテゴリごとの基準色（Water / Sky などの塊を見分けるための色分け）
        ImU32 CategoryColor(GpuTimingCategory category)
        {
            switch (category) {
            case GpuTimingCategory::Setup:           return IM_COL32(90, 90, 105, 255);
            case GpuTimingCategory::Shadow:          return IM_COL32(70, 70, 130, 255);
            case GpuTimingCategory::GBuffer:         return IM_COL32(140, 95, 40, 255);
            case GpuTimingCategory::PreLighting:     return IM_COL32(120, 70, 130, 255);
            case GpuTimingCategory::Lighting:        return IM_COL32(150, 120, 40, 255);
            case GpuTimingCategory::PostLighting:    return IM_COL32(110, 110, 45, 255);
            case GpuTimingCategory::Sky:             return IM_COL32(45, 105, 150, 255);
            case GpuTimingCategory::CloudDetail:     return IM_COL32(70, 125, 165, 255);
            case GpuTimingCategory::Transparent:     return IM_COL32(60, 130, 110, 255);
            case GpuTimingCategory::Water:           return IM_COL32(35, 110, 160, 255);
            case GpuTimingCategory::WaterSimulation: return IM_COL32(30, 85, 125, 255);
            case GpuTimingCategory::PostProcess:     return IM_COL32(130, 60, 90, 255);
            case GpuTimingCategory::Overlay:         return IM_COL32(80, 100, 80, 255);
            case GpuTimingCategory::Final:           return IM_COL32(100, 60, 60, 255);
            default:                                 return IM_COL32(85, 85, 85, 255);
            }
        }

        /// @brief 0..1 を緑→黄→赤のヒートマップ色へ変換する
        ImU32 HeatColor(float t)
        {
            t = std::clamp(t, 0.0f, 1.0f);
            float r = 0.0f;
            float g = 0.0f;
            if (t < 0.5f) {
                const float k = t * 2.0f;      // 緑 → 黄
                r = 0.25f + 0.65f * k;
                g = 0.62f;
            } else {
                const float k = (t - 0.5f) * 2.0f; // 黄 → 赤
                r = 0.90f;
                g = 0.62f - 0.52f * k;
            }
            return IM_COL32(static_cast<int>(r * 255.0f), static_cast<int>(g * 255.0f), 40, 255);
        }

        /// @brief 色を暗い背景側へ寄せる（フィルタ外・非ハイライトのノードを沈める）
        ImU32 Dim(ImU32 color, float amount)
        {
            const float keep = std::clamp(1.0f - amount, 0.0f, 1.0f);
            const int r = static_cast<int>(((color >> IM_COL32_R_SHIFT) & 0xFF) * keep + 32 * (1.0f - keep));
            const int g = static_cast<int>(((color >> IM_COL32_G_SHIFT) & 0xFF) * keep + 32 * (1.0f - keep));
            const int b = static_cast<int>(((color >> IM_COL32_B_SHIFT) & 0xFF) * keep + 32 * (1.0f - keep));
            return IM_COL32(r, g, b, 255);
        }

        /// @brief 色を明るくする（ホバー・選択時のタイトルバー用）
        ImU32 Brighten(ImU32 color, float amount)
        {
            const int r = std::min(255, static_cast<int>(((color >> IM_COL32_R_SHIFT) & 0xFF) * (1.0f + amount)));
            const int g = std::min(255, static_cast<int>(((color >> IM_COL32_G_SHIFT) & 0xFF) * (1.0f + amount)));
            const int b = std::min(255, static_cast<int>(((color >> IM_COL32_B_SHIFT) & 0xFF) * (1.0f + amount)));
            return IM_COL32(r, g, b, 255);
        }

        /// @brief 依存種別ごとのリンク色
        ImU32 DependencyColor(RenderGraphDependencyKind kind)
        {
            switch (kind) {
            case RenderGraphDependencyKind::ReadAfterWrite:  return IM_COL32(130, 160, 190, 200);
            case RenderGraphDependencyKind::WriteAfterWrite: return IM_COL32(205, 140, 60, 220);
            case RenderGraphDependencyKind::WriteAfterRead:  return IM_COL32(165, 120, 200, 220);
            default:                                         return IM_COL32(150, 150, 150, 200);
            }
        }

        /// @brief 長いリソース名をノード内に収まる長さへ切り詰める
        std::string Truncate(const std::string& text, size_t maxLength)
        {
            if (text.size() <= maxLength) {
                return text;
            }
            return text.substr(0, maxLength - 1) + "…";
        }

        /// @brief 大文字小文字を無視した部分一致
        bool ContainsIgnoreCase(const std::string& haystack, const char* needle)
        {
            if (!needle || needle[0] == '\0') {
                return true;
            }
            const std::string lowerHaystack = [&haystack] {
                std::string result = haystack;
                std::transform(result.begin(), result.end(), result.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return result;
            }();
            std::string lowerNeedle = needle;
            std::transform(lowerNeedle.begin(), lowerNeedle.end(), lowerNeedle.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return lowerHaystack.find(lowerNeedle) != std::string::npos;
        }

    /// @brief パス番号から imnodes のノード ID を作る
        int MakeNodeId(uint32_t passIndex)
        {
            return static_cast<int>(passIndex) + 1;
        }

    /// @brief パス番号とスロット番号から imnodes の属性 ID を作る
        int MakeAttributeId(uint32_t passIndex, int slot)
        {
            return MakeNodeId(passIndex) * 128 + slot;
        }

    } // anonymous namespace

    void RenderGraphEditorPanel::Initialize(EngineSystem* engine, GpuTimestampProfiler* profiler)
    {
        engine_ = engine;
        profiler_ = profiler;

        if (nodesContext_) {
            return;
        }

        nodesContext_ = ImNodes::CreateContext();
        ImNodes::SetCurrentContext(nodesContext_);
        ImNodes::StyleColorsDark();

        ImNodesStyle& style = ImNodes::GetStyle();
        style.NodeCornerRounding = 5.0f;
        style.NodePadding = ImVec2(8.0f, 6.0f);
        style.LinkThickness = 2.4f;
        style.PinCircleRadius = 3.6f;
        style.GridSpacing = 32.0f;

        // Alt + 左ドラッグでもパンできるようにする（中ボタンの無いデバイス向け）
        ImNodes::GetIO().EmulateThreeButtonMouse.Modifier = &ImGui::GetIO().KeyAlt;
        ImNodes::GetIO().MultipleSelectModifier.Modifier = &ImGui::GetIO().KeyCtrl;
    }

    void RenderGraphEditorPanel::Finalize()
    {
        // パネルが握っていた一括操作の上書きを残したままにしない
        RestoreEnableStates();

        if (RenderPipeline* pipeline = GetPipeline()) {
            pipeline->SetGraphCaptureEnabled(false);
        }

        if (nodesContext_) {
            ImNodes::DestroyContext(nodesContext_);
            nodesContext_ = nullptr;
        }
        engine_ = nullptr;
        profiler_ = nullptr;
    }

    RenderPipeline* RenderGraphEditorPanel::GetPipeline() const
    {
        return engine_ ? engine_->GetRenderPipeline() : nullptr;
    }

    void RenderGraphEditorPanel::SyncCaptureState()
    {
        RenderPipeline* pipeline = GetPipeline();
        if (!pipeline) {
            return;
        }

        // Draw() はウィンドウが開いていて畳まれていないときしか呼ばれない。
        // 前フレームに描かれていなければ複製を止め、記録コストをゼロへ戻す。
        if (!drawnRecently_ && pipeline->IsGraphCaptureEnabled()) {
            pipeline->SetGraphCaptureEnabled(false);
        }
        drawnRecently_ = false;
    }

    void RenderGraphEditorPanel::Draw()
    {
        RenderPipeline* pipeline = GetPipeline();
        if (!pipeline) {
            ImGui::TextDisabled("(RenderPipeline が存在しません)");
            return;
        }

        // Tools パネル既定の 460x540 ではノードグラフには狭すぎるため、初回だけ広げる
        //（ユーザーがリサイズすれば imgui.ini 側の値が優先される）
        ImGui::SetWindowSize(ImVec2(1180.0f, 720.0f), ImGuiCond_FirstUseEver);

        drawnRecently_ = true;
        if (!pipeline->IsGraphCaptureEnabled()) {
            pipeline->SetGraphCaptureEnabled(true);
        }
        pipeline->SetGraphCapturePaused(paused_);

        const std::vector<RenderGraphSnapshot>& snapshots = pipeline->GetGraphSnapshots();
        if (snapshots.empty()) {
            DrawToolbar(nullptr);
            ImGui::Separator();
            ImGui::TextDisabled("スナップショットを取得中… (次のフレームで表示されます)");
            return;
        }

        selectedViewIndex_ = std::clamp(selectedViewIndex_, 0, static_cast<int>(snapshots.size()) - 1);
        const RenderGraphSnapshot& snapshot = snapshots[selectedViewIndex_];

        RefreshTimings(snapshot);

        DrawToolbar(&snapshot);
        ImGui::Separator();

        // 構成が変わったフレームだけ再配置する。毎フレーム配置すると
        // ドラッグで動かしたノードが即座に戻ってしまい操作できない。
        const size_t topologyHash = ComputeTopologyHash(snapshot);
        if (layoutRequested_
            || topologyHash != layoutTopologyHash_
            || selectedViewIndex_ != layoutViewIndex_) {
            ComputeLayout(snapshot);
            layoutTopologyHash_ = topologyHash;
            layoutViewIndex_ = selectedViewIndex_;
            layoutRequested_ = false;
        }

        if (highlightDependencies_ && selectedPassIndex_ >= 0
            && highlightRootPass_ != selectedPassIndex_) {
            RebuildHighlightSets(snapshot, static_cast<uint32_t>(selectedPassIndex_));
        }

        const float detailWidth = std::min(340.0f, ImGui::GetContentRegionAvail().x * 0.38f);

        ImGui::BeginChild("##rg_graph", ImVec2(-(detailWidth + 8.0f), 0.0f), ImGuiChildFlags_Borders);
        DrawGraph(snapshot);
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("##rg_detail", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
        DrawDetailPanel(snapshot);
        ImGui::EndChild();

        DrawEnlargedPreview();
    }

    void RenderGraphEditorPanel::DrawToolbar(const RenderGraphSnapshot* snapshot)
    {
        RenderPipeline* pipeline = GetPipeline();

        // View 選択（1 フレーム内で補助 View → GameView の順に走る）
        if (pipeline) {
            const std::vector<RenderGraphSnapshot>& snapshots = pipeline->GetGraphSnapshots();
            std::string preview = snapshots.empty()
                ? std::string("(none)")
                : snapshots[std::clamp(selectedViewIndex_, 0, static_cast<int>(snapshots.size()) - 1)].displayName;

            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::BeginCombo("View", preview.c_str())) {
                for (int index = 0; index < static_cast<int>(snapshots.size()); ++index) {
                    const bool isSelected = (index == selectedViewIndex_);
                    if (ImGui::Selectable(snapshots[index].displayName.c_str(), isSelected)) {
                        selectedViewIndex_ = index;
                        selectedPassIndex_ = -1;
                        highlightRootPass_ = -1;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("パス数: %zu", snapshots[index].passes.size());
                    }
                }
                ImGui::EndCombo();
            }
        }

        ImGui::SameLine();
        if (ImGui::Checkbox("Pause", &paused_) && pipeline) {
            pipeline->SetGraphCapturePaused(paused_);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("スナップショットの更新を止めて、今の構成をじっくり眺める");
        }

        ImGui::SameLine();
        if (ImGui::Button("Auto Layout")) {
            layoutRequested_ = true;
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        int colorMode = static_cast<int>(colorMode_);
        if (ImGui::Combo("Color", &colorMode, "Category\0GPU Time\0")) {
            colorMode_ = static_cast<NodeColorMode>(colorMode);
        }

        ImGui::SameLine();
        ImGui::Checkbox("MiniMap", &showMiniMap_);

        ImGui::SameLine();
        ImGui::Checkbox("Highlight deps", &highlightDependencies_);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("選択したパスの上流・下流だけを点灯させる");
        }

        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputTextWithHint("##rg_filter", "パス名で絞り込み", nameFilter_, sizeof(nameFilter_));

        if (snapshot) {
            ImGui::SameLine();
            size_t executedCount = 0;
            size_t warningCount = 0;
            for (const RenderGraphSnapshotPass& pass : snapshot->passes) {
                if (pass.executed) {
                    ++executedCount;
                }
                if (!pass.unresolvedResources.empty()) {
                    ++warningCount;
                }
            }

            ImGui::Text("| %zu passes (%zu executed) / %.3f ms GPU",
                snapshot->passes.size(), executedCount, totalPassGpuMs_);

            if (warningCount > 0) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                    "| %zu パスで未解決リソース", warningCount);
            }
        }

        // 一括操作で有効状態を書き換えている間は、戻せることを常に見せておく
        if (!savedEnableStates_.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.30f, 0.15f, 1.0f));
            if (ImGui::Button("Restore pass enable states")) {
                RestoreEnableStates();
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                "パスの有効状態をエディタが上書き中 (%zu 件)", savedEnableStates_.size());
        }
    }

    void RenderGraphEditorPanel::DrawGraph(const RenderGraphSnapshot& snapshot)
    {
        ImNodes::SetCurrentContext(nodesContext_);
        linkInfos_.clear();

        ImNodes::BeginNodeEditor();

        const size_t passCount = snapshot.passes.size();
        for (uint32_t passIndex = 0; passIndex < passCount; ++passIndex) {
            // 自動配置は再レイアウトした直後の 1 フレームだけ書き戻す。
            // 毎フレーム書くとドラッグしたノードが即座に戻って掴めなくなる。
            if (applyLayoutPositions_ && passIndex < nodePositions_.size()) {
                ImNodes::SetNodeGridSpacePos(MakeNodeId(passIndex), nodePositions_[passIndex]);
            }
            DrawNode(snapshot, passIndex);
        }
        applyLayoutPositions_ = false;

        // 依存エッジ。原因リソースのピン同士を結ぶので、どのリソースで縛られたかが線で分かる。
        for (uint32_t passIndex = 0; passIndex < passCount; ++passIndex) {
            const RenderGraphSnapshotPass& pass = snapshot.passes[passIndex];

            for (const RenderGraphDependency& dependency : pass.dependencies) {
                if (dependency.passIndex >= passCount) {
                    continue;
                }
                const RenderGraphSnapshotPass& fromPass = snapshot.passes[dependency.passIndex];

                // 依存元は「そのリソースを書いた側」が基本。WAR だけは読んだ側が起点になる。
                int startAttribute = -1;
                for (size_t index = 0; index < fromPass.writes.size() && index < kMaxPinsPerSide; ++index) {
                    if (fromPass.writes[index].resourceName == dependency.resourceName) {
                        startAttribute = MakeAttributeId(dependency.passIndex,
                            kMaxPinsPerSide + static_cast<int>(index));
                        break;
                    }
                }
                if (startAttribute < 0) {
                    for (size_t index = 0; index < fromPass.reads.size() && index < kMaxPinsPerSide; ++index) {
                        if (fromPass.reads[index].resourceName == dependency.resourceName) {
                            startAttribute = MakeAttributeId(dependency.passIndex, static_cast<int>(index));
                            break;
                        }
                    }
                }

                int endAttribute = -1;
                for (size_t index = 0; index < pass.reads.size() && index < kMaxPinsPerSide; ++index) {
                    if (pass.reads[index].resourceName == dependency.resourceName) {
                        endAttribute = MakeAttributeId(passIndex, static_cast<int>(index));
                        break;
                    }
                }
                if (endAttribute < 0) {
                    for (size_t index = 0; index < pass.writes.size() && index < kMaxPinsPerSide; ++index) {
                        if (pass.writes[index].resourceName == dependency.resourceName) {
                            endAttribute = MakeAttributeId(passIndex,
                                kMaxPinsPerSide + static_cast<int>(index));
                            break;
                        }
                    }
                }

                if (startAttribute < 0 || endAttribute < 0) {
                    continue; // ピン上限を超えて集約されたリソース
                }

                const bool dimmed = !IsHighlighted(passIndex) && !IsHighlighted(dependency.passIndex);

                ImU32 linkColor = DependencyColor(dependency.kind);
                if (dimmed) {
                    linkColor = Dim(linkColor, 0.75f);
                }

                const int linkId = static_cast<int>(linkInfos_.size()) + 1;
                linkInfos_.push_back({ dependency.passIndex, passIndex,
                    dependency.resourceName, ToString(dependency.kind) });

                ImNodes::PushColorStyle(ImNodesCol_Link, linkColor);
                ImNodes::PushColorStyle(ImNodesCol_LinkHovered, Brighten(linkColor, 0.5f));
                ImNodes::PushColorStyle(ImNodesCol_LinkSelected, IM_COL32(255, 230, 140, 255));
                ImNodes::Link(linkId, startAttribute, endAttribute);
                ImNodes::PopColorStyle();
                ImNodes::PopColorStyle();
                ImNodes::PopColorStyle();
            }
        }

        // 左上から右へ伸びるレイアウトなので、ミニマップは右下へ置く
        //（右上だと最終段のノードに重なって隠してしまう）
        if (showMiniMap_) {
            ImNodes::MiniMap(0.18f, ImNodesMiniMapLocation_BottomRight);
        }

        ImNodes::EndNodeEditor();

        // ── EndNodeEditor 後でないと問い合わせられない情報 ──
        int hoveredLinkId = -1;
        if (ImNodes::IsLinkHovered(&hoveredLinkId)) {
            const size_t linkIndex = static_cast<size_t>(hoveredLinkId - 1);
            if (linkIndex < linkInfos_.size()) {
                const LinkInfo& info = linkInfos_[linkIndex];
                ImGui::BeginTooltip();
                ImGui::Text("%s → %s",
                    snapshot.passes[info.fromPass].name.c_str(),
                    snapshot.passes[info.toPass].name.c_str());
                ImGui::Separator();
                ImGui::Text("原因リソース: %s", info.resourceName.c_str());
                ImGui::Text("依存種別: %s", info.kindLabel);
                ImGui::EndTooltip();
            }
        }

        int hoveredNodeId = -1;
        const bool nodeHovered = ImNodes::IsNodeHovered(&hoveredNodeId);
        if (nodeHovered) {
            const uint32_t passIndex = static_cast<uint32_t>(hoveredNodeId - 1);
            if (passIndex < snapshot.passes.size()) {
                const RenderGraphSnapshotPass& pass = snapshot.passes[passIndex];

                ImGui::BeginTooltip();
                ImGui::TextUnformatted(pass.name.c_str());
                ImGui::Separator();
                ImGui::Text("カテゴリ: %s", GpuTimestampProfiler::GetCategoryLabel(pass.timingCategory));
                ImGui::Text("GPU %.3f ms / CPU %.3f ms", PassGpuMs(passIndex), PassCpuMs(passIndex));
                ImGui::Text("レイヤ: %d  Read %zu / Write %zu",
                    passIndex < nodeDepths_.size() ? nodeDepths_[passIndex] : 0,
                    pass.reads.size(), pass.writes.size());
                if (!pass.executed) {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "今フレームは実行されていない");
                }
                if (!pass.unresolvedResources.empty()) {
                    ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                        "未解決リソース %zu 件（バリア未発行）", pass.unresolvedResources.size());
                }
                ImGui::TextDisabled("右クリックで操作メニュー");
                ImGui::EndTooltip();
            }
        }

        if (nodeHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            contextMenuPassIndex_ = hoveredNodeId - 1;
            ImGui::OpenPopup("##rg_node_context");
        }
        DrawNodeContextMenu(snapshot);

        // 選択（複数選択時は先頭を詳細に出す）
        const int selectedCount = ImNodes::NumSelectedNodes();
        if (selectedCount > 0) {
            std::vector<int> selectedNodeIds(static_cast<size_t>(selectedCount));
            ImNodes::GetSelectedNodes(selectedNodeIds.data());
            const int passIndex = selectedNodeIds[0] - 1;
            if (passIndex >= 0 && passIndex < static_cast<int>(snapshot.passes.size())) {
                selectedPassIndex_ = passIndex;
            }
        }
    }

    void RenderGraphEditorPanel::DrawNode(const RenderGraphSnapshot& snapshot, uint32_t passIndex)
    {
        const RenderGraphSnapshotPass& pass = snapshot.passes[passIndex];
        const float gpuMs = PassGpuMs(passIndex);

        // ── 色を決める ──
        ImU32 titleColor = CategoryColor(pass.timingCategory);
        if (colorMode_ == NodeColorMode::GpuTime) {
            titleColor = HeatColor(gpuMs / maxPassGpuMs_);
        }

        const bool matchesFilter = ContainsIgnoreCase(pass.name, nameFilter_);
        const bool inHighlight = IsHighlighted(passIndex);
        const bool disabled = (pass.renderPass && !pass.renderPass->IsEnabled());

        float dimAmount = 0.0f;
        if (!matchesFilter) {
            dimAmount = std::max(dimAmount, 0.75f);
        }
        if (!inHighlight) {
            dimAmount = std::max(dimAmount, 0.65f);
        }
        if (!pass.executed || disabled) {
            dimAmount = std::max(dimAmount, 0.55f);
        }
        if (dimAmount > 0.0f) {
            titleColor = Dim(titleColor, dimAmount);
        }

        const bool hasWarning = !pass.unresolvedResources.empty();

        ImNodes::PushColorStyle(ImNodesCol_TitleBar, titleColor);
        ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, Brighten(titleColor, 0.35f));
        ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, Brighten(titleColor, 0.55f));
        ImNodes::PushColorStyle(ImNodesCol_NodeOutline,
            hasWarning ? IM_COL32(235, 90, 70, 255) : IM_COL32(60, 60, 60, 255));

        ImNodes::BeginNode(MakeNodeId(passIndex));

        ImNodes::BeginNodeTitleBar();
        if (disabled) {
            ImGui::TextDisabled("%s [OFF]", pass.name.c_str());
        } else if (hasWarning) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "%s !", pass.name.c_str());
        } else {
            ImGui::TextUnformatted(pass.name.c_str());
        }
        ImNodes::EndNodeTitleBar();

        // ノード幅を揃える（出力側の右寄せ計算の基準になる）
        ImGui::Dummy(ImVec2(kNodeContentWidth, 1.0f));

        // 入力ピン = Read 宣言
        const size_t readCount = std::min<size_t>(pass.reads.size(), kMaxPinsPerSide);
        for (size_t index = 0; index < readCount; ++index) {
            ImNodes::BeginInputAttribute(
                MakeAttributeId(passIndex, static_cast<int>(index)), ImNodesPinShape_CircleFilled);
            ImGui::TextUnformatted(Truncate(pass.reads[index].resourceName, 24).c_str());
            ImNodes::EndInputAttribute();
        }
        if (pass.reads.size() > readCount) {
            ImGui::TextDisabled("… +%zu reads", pass.reads.size() - readCount);
        }

        // 計測値（ピンを持たない静的行）
        ImNodes::BeginStaticAttribute(MakeAttributeId(passIndex, 127));
        if (pass.executed) {
            ImGui::Text("GPU %.3f ms", gpuMs);
        } else {
            ImGui::TextDisabled("not executed");
        }
        ImNodes::EndStaticAttribute();

        // 出力ピン = Write 宣言（右寄せ）
        const size_t writeCount = std::min<size_t>(pass.writes.size(), kMaxPinsPerSide);
        for (size_t index = 0; index < writeCount; ++index) {
            ImNodes::BeginOutputAttribute(
                MakeAttributeId(passIndex, kMaxPinsPerSide + static_cast<int>(index)),
                ImNodesPinShape_QuadFilled);
            const std::string label = Truncate(pass.writes[index].resourceName, 24);
            const float indent = std::max(0.0f, kNodeContentWidth - ImGui::CalcTextSize(label.c_str()).x);
            ImGui::Indent(indent);
            ImGui::TextUnformatted(label.c_str());
            ImGui::Unindent(indent);
            ImNodes::EndOutputAttribute();
        }
        if (pass.writes.size() > writeCount) {
            ImGui::TextDisabled("… +%zu writes", pass.writes.size() - writeCount);
        }

        ImNodes::EndNode();

        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
    }

    void RenderGraphEditorPanel::DrawNodeContextMenu(const RenderGraphSnapshot& snapshot)
    {
        // 右クリックしたノードに対する操作（有効/無効・依存のハイライト・出力プレビュー）
        if (!ImGui::BeginPopup("##rg_node_context")) {
            return;
        }

        const uint32_t passIndex = static_cast<uint32_t>(contextMenuPassIndex_);
        if (contextMenuPassIndex_ < 0 || passIndex >= snapshot.passes.size()) {
            ImGui::EndPopup();
            return;
        }

        const RenderGraphSnapshotPass& pass = snapshot.passes[passIndex];
        ImGui::TextUnformatted(pass.name.c_str());
        ImGui::Separator();

        if (ImGui::MenuItem("詳細を表示")) {
            selectedPassIndex_ = static_cast<int>(passIndex);
            RebuildHighlightSets(snapshot, passIndex);
        }

        if (pass.transient) {
            ImGui::TextDisabled("(PostEffect ノードは PostEffect 側で切り替え)");
        } else if (pass.renderPass) {
            const bool enabled = pass.renderPass->IsEnabled();
            if (ImGui::MenuItem(enabled ? "このパスを無効化" : "このパスを有効化")) {
                SaveEnableStates(snapshot);
                pass.renderPass->SetEnabled(!enabled);
            }

            if (ImGui::MenuItem("このパスだけ実行 (Solo)")) {
                SaveEnableStates(snapshot);
                for (const RenderGraphSnapshotPass& other : snapshot.passes) {
                    if (other.renderPass) {
                        other.renderPass->SetEnabled(other.renderPass == pass.renderPass);
                    }
                }
            }

            if (ImGui::MenuItem("ここより下流を無効化")) {
                SaveEnableStates(snapshot);
                RebuildHighlightSets(snapshot, passIndex);
                for (uint32_t index = 0; index < snapshot.passes.size(); ++index) {
                    if (index == passIndex || !downstreamMask_[index]) {
                        continue;
                    }
                    if (RenderPass* target = snapshot.passes[index].renderPass) {
                        target->SetEnabled(false);
                    }
                }
            }
        }

        if (!savedEnableStates_.empty()) {
            ImGui::Separator();
            if (ImGui::MenuItem("有効状態を元に戻す")) {
                RestoreEnableStates();
            }
        }

        ImGui::EndPopup();
    }

    void RenderGraphEditorPanel::DrawDetailPanel(const RenderGraphSnapshot& snapshot)
    {
        // グラフ本体は imnodes が描くので、ここは選択中ノードの内訳に徹する
        if (ImGui::BeginTabBar("##rg_detail_tabs")) {

            // Pass タブ: 選択ノードの計測値・入出力・依存・発行バリアを並べる。
            // 「なぜこの順番か」を依存の一覧で示すのがこのパネルの主目的
            if (ImGui::BeginTabItem("Pass")) {
                if (selectedPassIndex_ < 0
                    || selectedPassIndex_ >= static_cast<int>(snapshot.passes.size())) {
                    ImGui::TextDisabled("ノードをクリックすると詳細を表示します");
                } else {
                    const uint32_t passIndex = static_cast<uint32_t>(selectedPassIndex_);
                    const RenderGraphSnapshotPass& pass = snapshot.passes[passIndex];

                    ImGui::SeparatorText(pass.name.c_str());
                    ImGui::Text("カテゴリ : %s", GpuTimestampProfiler::GetCategoryLabel(pass.timingCategory));
                    ImGui::Text("レイヤ   : %d",
                        passIndex < nodeDepths_.size() ? nodeDepths_[passIndex] : 0);
                    ImGui::Text("GPU      : %.3f ms", PassGpuMs(passIndex));
                    ImGui::Text("CPU      : %.3f ms", PassCpuMs(passIndex));
                    ImGui::Text("実行     : %s", pass.executed ? "はい" : "いいえ");

                    if (pass.renderPass && !pass.transient) {
                        bool enabled = pass.renderPass->IsEnabled();
                        if (ImGui::Checkbox("Enabled", &enabled)) {
                            SaveEnableStates(snapshot);
                            pass.renderPass->SetEnabled(enabled);
                        }
                    }

                    if (!pass.unresolvedResources.empty()) {
                        ImGui::SeparatorText("未解決リソース (バリア未発行)");
                        for (const std::string& name : pass.unresolvedResources) {
                            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s", name.c_str());
                        }
                    }

                    ImGui::SeparatorText("Read");
                    for (const RenderGraphResourceAccess& access : pass.reads) {
                        ImGui::BulletText("%s  [%s]",
                            access.resourceName.c_str(),
                            ResourceStateToShortString(access.requiredState));
                    }
                    if (pass.reads.empty()) {
                        ImGui::TextDisabled("(なし)");
                    }

                    ImGui::SeparatorText("Write");
                    for (const RenderGraphResourceAccess& access : pass.writes) {
                        ImGui::BulletText("%s  [%s]",
                            access.resourceName.c_str(),
                            ResourceStateToShortString(access.requiredState));

                        if (const RenderGraphSnapshotResource* resource
                            = snapshot.FindResource(access.resourceName);
                            resource && resource->srvHandle.ptr != 0) {
                            ImGui::PushID(access.resourceName.c_str());
                            ImGui::Image(static_cast<ImTextureID>(resource->srvHandle.ptr),
                                ImVec2(150.0f, 84.0f));
                            if (ImGui::IsItemClicked()) {
                                enlargedTitle_ = access.resourceName;
                                enlargedSrv_ = resource->srvHandle;
                            }
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("クリックで拡大");
                            }
                            ImGui::PopID();
                        }
                    }
                    if (pass.writes.empty()) {
                        ImGui::TextDisabled("(なし)");
                    }

                    ImGui::SeparatorText("依存（なぜこの順番か）");
                    if (pass.dependencies.empty()) {
                        ImGui::TextDisabled("(先行パスなし)");
                    }
                    for (const RenderGraphDependency& dependency : pass.dependencies) {
                        if (dependency.passIndex >= snapshot.passes.size()) {
                            continue;
                        }
                        ImGui::BulletText("%s ← %s (%s)",
                            ToString(dependency.kind),
                            snapshot.passes[dependency.passIndex].name.c_str(),
                            dependency.resourceName.c_str());
                    }

                    ImGui::SeparatorText("発行したバリア");
                    if (pass.barriers.empty()) {
                        ImGui::TextDisabled("(なし)");
                    }
                    for (const RenderGraphBarrierRecord& barrier : pass.barriers) {
                        if (barrier.isUavBarrier) {
                            ImGui::BulletText("%s : UAV バリア", barrier.resourceName.c_str());
                        } else if (barrier.beforeState == barrier.afterState) {
                            ImGui::BulletText("%s : %s (遷移なし)",
                                barrier.resourceName.c_str(),
                                ResourceStateToShortString(barrier.beforeState));
                        } else {
                            ImGui::BulletText("%s : %s → %s",
                                barrier.resourceName.c_str(),
                                ResourceStateToShortString(barrier.beforeState),
                                ResourceStateToShortString(barrier.afterState));
                        }
                    }
                }
                ImGui::EndTabItem();
            }

            // Resources タブ: 論理リソースごとの生存区間と解決状況
            if (ImGui::BeginTabItem("Resources")) {
                ImGui::Text("論理リソース %zu 件", snapshot.resources.size());
                if (ImGui::BeginTable("##rg_resources", 4,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
                    ImGui::TableSetupColumn("Name");
                    ImGui::TableSetupColumn("W/R", ImGuiTableColumnFlags_WidthFixed, 46.0f);
                    ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 96.0f);
                    ImGui::TableSetupColumn("解決", ImGuiTableColumnFlags_WidthFixed, 40.0f);
                    ImGui::TableHeadersRow();

                    for (const RenderGraphSnapshotResource& resource : snapshot.resources) {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(resource.name.c_str());
                        if (resource.srvHandle.ptr != 0 && ImGui::IsItemClicked()) {
                            enlargedTitle_ = resource.name;
                            enlargedSrv_ = resource.srvHandle;
                        }
                        ImGui::TableNextColumn();
                        ImGui::Text("%u/%u", resource.writerCount, resource.readerCount);
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(ResourceStateToShortString(resource.stateAtCapture));
                        ImGui::TableNextColumn();
                        if (resource.resolved) {
                            ImGui::TextUnformatted("OK");
                        } else {
                            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "NG");
                        }
                    }
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }

            // Order タブ: RenderGraph が導出した実行順を、選択と連動する表で出す
            if (ImGui::BeginTabItem("Order")) {
                ImGui::TextDisabled("依存を満たす範囲で登録順に近い実行順");
                if (ImGui::BeginTable("##rg_order", 3,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
                    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 28.0f);
                    ImGui::TableSetupColumn("Pass");
                    ImGui::TableSetupColumn("GPU ms", ImGuiTableColumnFlags_WidthFixed, 64.0f);
                    ImGui::TableHeadersRow();

                    int order = 0;
                    for (uint32_t passIndex : snapshot.executionOrder) {
                        if (passIndex >= snapshot.passes.size()) {
                            continue;
                        }
                        const RenderGraphSnapshotPass& pass = snapshot.passes[passIndex];

                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::Text("%d", order++);
                        ImGui::TableNextColumn();
                        if (ImGui::Selectable(pass.name.c_str(),
                            selectedPassIndex_ == static_cast<int>(passIndex),
                            ImGuiSelectableFlags_SpanAllColumns)) {
                            selectedPassIndex_ = static_cast<int>(passIndex);
                            RebuildHighlightSets(snapshot, passIndex);
                            ImNodes::ClearNodeSelection();
                            ImNodes::SelectNode(MakeNodeId(passIndex));
                            ImNodes::EditorContextMoveToNode(MakeNodeId(passIndex));
                        }
                        ImGui::TableNextColumn();
                        ImGui::Text("%.3f", PassGpuMs(passIndex));
                    }
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }

    void RenderGraphEditorPanel::DrawEnlargedPreview()
    {
        if (enlargedSrv_.ptr == 0) {
            return;
        }

        bool open = true;
        ImGui::SetNextWindowSize(ImVec2(700.0f, 430.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(("RenderGraph Preview: " + enlargedTitle_).c_str(), &open)) {
            const ImVec2 available = ImGui::GetContentRegionAvail();
            ImGui::Image(static_cast<ImTextureID>(enlargedSrv_.ptr), available);
        }
        ImGui::End();

        if (!open) {
            enlargedSrv_ = {};
            enlargedTitle_.clear();
        }
    }

    void RenderGraphEditorPanel::ComputeLayout(const RenderGraphSnapshot& snapshot)
    {
        const size_t passCount = snapshot.passes.size();
        nodePositions_.assign(passCount, ImVec2(0.0f, 0.0f));
        nodeDepths_.assign(passCount, 0);
        upstreamMask_.assign(passCount, 0);
        downstreamMask_.assign(passCount, 0);
        highlightRootPass_ = -1;
        applyLayoutPositions_ = true;

        if (passCount == 0) {
            return;
        }

        // 走査順はトポロジカル順。依存元の深さが必ず先に確定するため、
        // 最長経路によるレイヤ割り当てが 1 パスで済む。
        std::vector<uint32_t> traversal = snapshot.executionOrder;
        if (traversal.size() != passCount) {
            // 循環検出時のフォールバック（登録順）でも破綻しないようにする
            traversal.resize(passCount);
            std::iota(traversal.begin(), traversal.end(), 0u);
        }

        int maxDepth = 0;
        for (uint32_t passIndex : traversal) {
            if (passIndex >= passCount) {
                continue;
            }
            for (const RenderGraphDependency& dependency : snapshot.passes[passIndex].dependencies) {
                if (dependency.passIndex >= passCount) {
                    continue;
                }
                nodeDepths_[passIndex] = std::max(
                    nodeDepths_[passIndex], nodeDepths_[dependency.passIndex] + 1);
            }
            maxDepth = std::max(maxDepth, nodeDepths_[passIndex]);
        }

        std::vector<std::vector<uint32_t>> layers(static_cast<size_t>(maxDepth) + 1);
        for (uint32_t passIndex : traversal) {
            if (passIndex < passCount) {
                layers[static_cast<size_t>(nodeDepths_[passIndex])].push_back(passIndex);
            }
        }

        // バリセンター法: 各ノードを「依存元の行位置の平均」の順に並べ替えると線の交差が減る。
        // 依存元は必ず浅いレイヤにあるので、レイヤを浅い順に処理すれば行位置は確定済み。
        std::vector<float> rowOfPass(passCount, 0.0f);
        for (size_t layerIndex = 0; layerIndex < layers.size(); ++layerIndex) {
            std::vector<uint32_t>& layer = layers[layerIndex];

            if (layerIndex > 0) {
                std::stable_sort(layer.begin(), layer.end(),
                    [&snapshot, &rowOfPass, passCount](uint32_t lhs, uint32_t rhs) {
                        auto barycenter = [&](uint32_t passIndex) {
                            float sum = 0.0f;
                            int count = 0;
                            for (const RenderGraphDependency& dependency
                                : snapshot.passes[passIndex].dependencies) {
                                if (dependency.passIndex >= passCount) {
                                    continue;
                                }
                                sum += rowOfPass[dependency.passIndex];
                                ++count;
                            }
                            return count > 0 ? sum / static_cast<float>(count) : 0.0f;
                        };
                        return barycenter(lhs) < barycenter(rhs);
                    });
            }

            for (size_t rowIndex = 0; rowIndex < layer.size(); ++rowIndex) {
                rowOfPass[layer[rowIndex]] = static_cast<float>(rowIndex);
            }
        }

        // レイヤごとに縦方向へセンタリングすると、幅の違うレイヤが並んでも読みやすい
        for (size_t layerIndex = 0; layerIndex < layers.size(); ++layerIndex) {
            const std::vector<uint32_t>& layer = layers[layerIndex];
            const float layerHeight = static_cast<float>(layer.size() - 1) * rowSpacing_;

            for (size_t rowIndex = 0; rowIndex < layer.size(); ++rowIndex) {
                nodePositions_[layer[rowIndex]] = ImVec2(
                    static_cast<float>(layerIndex) * columnSpacing_,
                    static_cast<float>(rowIndex) * rowSpacing_ - layerHeight * 0.5f);
            }
        }
    }

    size_t RenderGraphEditorPanel::ComputeTopologyHash(const RenderGraphSnapshot& snapshot)
    {
        size_t hash = std::hash<size_t>{}(snapshot.passes.size());
        const std::hash<std::string> stringHasher;

        auto combine = [&hash](size_t value) {
            hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
            };

        for (const RenderGraphSnapshotPass& pass : snapshot.passes) {
            combine(stringHasher(pass.name));
            combine(pass.dependencies.size());
            combine(pass.reads.size() * 31 + pass.writes.size());
        }
        return hash;
    }

    void RenderGraphEditorPanel::RebuildHighlightSets(const RenderGraphSnapshot& snapshot, uint32_t rootPass)
    {
        const size_t passCount = snapshot.passes.size();
        upstreamMask_.assign(passCount, 0);
        downstreamMask_.assign(passCount, 0);
        highlightRootPass_ = static_cast<int>(rootPass);

        if (rootPass >= passCount) {
            return;
        }

        upstreamMask_[rootPass] = 1;
        downstreamMask_[rootPass] = 1;

        // 上流: dependencies を逆向きに辿る。依存は必ず若いインデックスを指すため、
        // 降順に 1 回舐めれば推移閉包が求まる。
        for (uint32_t passIndex = rootPass + 1; passIndex-- > 0;) {
            if (!upstreamMask_[passIndex]) {
                continue;
            }
            for (const RenderGraphDependency& dependency : snapshot.passes[passIndex].dependencies) {
                if (dependency.passIndex < passCount) {
                    upstreamMask_[dependency.passIndex] = 1;
                }
            }
        }

        // 下流: 昇順に舐めて「依存元が点いていれば自分も点ける」
        for (uint32_t passIndex = rootPass; passIndex < passCount; ++passIndex) {
            if (downstreamMask_[passIndex]) {
                continue;
            }
            for (const RenderGraphDependency& dependency : snapshot.passes[passIndex].dependencies) {
                if (dependency.passIndex < passCount && downstreamMask_[dependency.passIndex]) {
                    downstreamMask_[passIndex] = 1;
                    break;
                }
            }
        }
    }

    void RenderGraphEditorPanel::LookupTiming(const std::string& timingLabel, float& outGpuMs, float& outCpuMs) const
    {
        outGpuMs = 0.0f;
        outCpuMs = 0.0f;
        if (!profiler_ || !profiler_->IsInitialized()) {
            return;
        }

        for (const GpuTimingResult& result : profiler_->GetResults()) {
            if (result.name && timingLabel == result.name) {
                outGpuMs = result.gpuMs;
                outCpuMs = result.cpuMs;
                return;
            }
        }
    }

    void RenderGraphEditorPanel::RefreshTimings(const RenderGraphSnapshot& snapshot)
    {
        const size_t passCount = snapshot.passes.size();
        passGpuMs_.assign(passCount, 0.0f);
        passCpuMs_.assign(passCount, 0.0f);
        totalPassGpuMs_ = 0.0f;

        // 全パスが軽いフレームで最速パスまで真っ赤にならないよう、正規化の下限を噛ませる
        maxPassGpuMs_ = 0.5f;

        for (size_t passIndex = 0; passIndex < passCount; ++passIndex) {
            LookupTiming(snapshot.MakeTimingLabel(snapshot.passes[passIndex].name),
                passGpuMs_[passIndex], passCpuMs_[passIndex]);
            totalPassGpuMs_ += passGpuMs_[passIndex];
            maxPassGpuMs_ = std::max(maxPassGpuMs_, passGpuMs_[passIndex]);
        }
    }

    void RenderGraphEditorPanel::SaveEnableStates(const RenderGraphSnapshot& snapshot)
    {
        if (!savedEnableStates_.empty()) {
            return; // 最初の一括操作の直前の状態へ戻したいので、上書きしない
        }

        for (const RenderGraphSnapshotPass& pass : snapshot.passes) {
            if (pass.renderPass && !pass.transient) {
                savedEnableStates_[pass.renderPass] = pass.renderPass->IsEnabled();
            }
        }
    }

    void RenderGraphEditorPanel::RestoreEnableStates()
    {
        RenderPipeline* pipeline = GetPipeline();
        if (!pipeline) {
            savedEnableStates_.clear();
            return;
        }

        // 保存後にシーンが破棄されているとポインタが死んでいる可能性がある。
        // 現在パイプラインに載っているパスとの照合を通してから書き戻す。
        for (const RenderGraphSnapshot& snapshot : pipeline->GetGraphSnapshots()) {
            for (const RenderGraphSnapshotPass& pass : snapshot.passes) {
                if (!pass.renderPass || pass.transient) {
                    continue;
                }
                if (auto it = savedEnableStates_.find(pass.renderPass); it != savedEnableStates_.end()) {
                    pass.renderPass->SetEnabled(it->second);
                }
            }
        }

        savedEnableStates_.clear();
    }
}

#endif // USE_IMGUI
