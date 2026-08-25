#include "pch.h"
#include "EngineStatsWindow.h"

#include "Diagnostics/EngineStats.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/Render/Culling/HiZOcclusionSystem.h"
#include "Graphics/Render/RenderOptimizationSettings.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "EngineSystem/EngineSystem.h"
#include "Utility/FrameRate/FrameRateController.h"
#include "Scene/SceneManager.h"
#include "GameObject/GameObjectManager.h"

#include <imgui.h>

#include <algorithm>
#include <Windows.h>
#include <Psapi.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

namespace CoreEngine
{
    namespace
    {
        // 視認性向上：値の大小に応じた色付け
        ImVec4 GetFpsColor(float fps)
        {
            if (fps >= 58.0f) return ImVec4(0.4f, 1.0f, 0.4f, 1.0f); // 緑
            if (fps >= 30.0f) return ImVec4(1.0f, 0.85f, 0.3f, 1.0f); // 黄
            return ImVec4(1.0f, 0.4f, 0.4f, 1.0f); // 赤
        }

        // Unity風カラー定数
        static constexpr ImVec4 kLabelColor = ImVec4(0.90f, 0.90f, 0.90f, 1.0f); // 明るいグレー（ラベル）
        static constexpr ImVec4 kValueColor = ImVec4(1.00f, 1.00f, 1.00f, 1.0f); // 白（値）
        static constexpr ImVec4 kSubLabel = ImVec4(0.78f, 0.78f, 0.78f, 1.0f); // サブ項目ラベル
        static constexpr ImVec4 kIdleColor = ImVec4(0.45f, 0.45f, 0.45f, 1.0f); // 今フレーム実行されなかった行
        static constexpr ImVec4 kHeaderColor = ImVec4(1.0f, 0.65f, 0.0f, 1.0f); // Unityオレンジ（ヘッダ）

        // 通常行：ラベル左、値右寄せ
        void DrawKeyValue(const char* key, const char* fmt, ...)
        {
            char buf[128];
            va_list args;
            va_start(args, fmt);
            vsnprintf(buf, sizeof(buf), fmt, args);
            va_end(args);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(kLabelColor, "%s", key);
            ImGui::TableSetColumnIndex(1);
            float valueWidth = ImGui::CalcTextSize(buf).x;
            float colWidth = ImGui::GetColumnWidth();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (std::max)(0.0f, colWidth - valueWidth - 4.0f));
            ImGui::TextColored(kValueColor, "%s", buf);
        }

        // サブ項目行
        void DrawSubKeyValue(const char* key, const char* fmt, ...)
        {
            char buf[128];
            va_list args;
            va_start(args, fmt);
            vsnprintf(buf, sizeof(buf), fmt, args);
            va_end(args);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Indent(12.0f);
            ImGui::TextColored(kSubLabel, "%s", key);
            ImGui::Unindent(12.0f);
            ImGui::TableSetColumnIndex(1);
            float valueWidth = ImGui::CalcTextSize(buf).x;
            float colWidth = ImGui::GetColumnWidth();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (std::max)(0.0f, colWidth - valueWidth - 4.0f));
            ImGui::TextColored(kSubLabel, "%s", buf);
        }

        // テーブルフラグ
        static constexpr ImGuiTableFlags kTableFlags =
            ImGuiTableFlags_SizingStretchProp |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_BordersInnerH |
            ImGuiTableFlags_PadOuterX;

    /// @brief バイト数を KB / MB / GB の読みやすい文字列にする
        const char* FormatBytes(uint64_t bytes, char* buf, size_t bufSize)
        {
            const double KB = 1024.0;
            const double MB = KB * 1024.0;
            const double GB = MB * 1024.0;
            if (bytes >= static_cast<uint64_t>(GB))
            {
                snprintf(buf, bufSize, "%.2f GB", static_cast<double>(bytes) / GB);
            } else if (bytes >= static_cast<uint64_t>(MB))
            {
                snprintf(buf, bufSize, "%.2f MB", static_cast<double>(bytes) / MB);
            } else if (bytes >= static_cast<uint64_t>(KB))
            {
                snprintf(buf, bufSize, "%.2f KB", static_cast<double>(bytes) / KB);
            } else
            {
                snprintf(buf, bufSize, "%llu B", static_cast<unsigned long long>(bytes));
            }
            return buf;
        }
    }

    void EngineStatsWindow::Collect()
    {
        // リソースキャッシュ統計の更新
        if (modelManager_)
        {
            modelManager_->UpdateResourceCacheStats();
        }
        CollectSceneStats();
        CollectMemoryStats();

        // FPS 履歴更新
        if (engine_)
        {
            if (auto* fc = engine_->GetService<FrameRateController>())
            {
                snapshotFps_ = fc->GetCurrentFPS();
                snapshotDeltaTimeMs_ = fc->GetDeltaTime() * 1000.0f;
                snapshotTargetFps_ = fc->GetTargetFPS();
                fpsHistory_[fpsHistoryIndex_] = snapshotFps_;
                fpsHistoryIndex_ = (fpsHistoryIndex_ + 1) % kFpsHistorySize;
            }
        }

        // 描画完了後のスナップショット保存（Draw 時はこちらを参照）
        snapshotRender_ = EngineStats::GetInstance().GetRenderStats();
        snapshotScene_ = EngineStats::GetInstance().GetSceneStats();
        snapshotResource_ = EngineStats::GetInstance().GetResourceCacheStats();
        snapshotMemory_ = EngineStats::GetInstance().GetMemoryStats();

        // GPU タイミングスナップショット
        if (gpuProfiler_ && gpuProfiler_->IsInitialized())
        {
            snapshotGpu_ = gpuProfiler_->GetResults();
            // フレーム合計の履歴を記録
            const uint32_t totalSlot = static_cast<uint32_t>(GpuTimestampSlot::Total);
            gpuTotalHistory_[gpuHistoryIndex_] = snapshotGpu_[totalSlot].gpuMs;
            gpuHistoryIndex_ = (gpuHistoryIndex_ + 1) % kGpuHistorySize;

            // 一定間隔でパス別タイミングを更新（毎フレーム変化を抑制）
            timingAccumTime_ += snapshotDeltaTimeMs_ * 0.001f; // ms → 秒
            if (timingAccumTime_ >= kTimingUpdateInterval)
            {
                timingAccumTime_ = 0.0f;
                frozenGpu_ = snapshotGpu_;
            }

            // 統計計測へは表示用に間引いた frozenGpu_ ではなく毎フレームの生値を渡す。
            timingCapture_.Tick(snapshotGpu_, snapshotDeltaTimeMs_, snapshotFps_);
        }
    }

    void EngineStatsWindow::DrawPerformanceTab()
    {
        const float currentFPS = snapshotFps_;
        const float deltaTimeMs = snapshotDeltaTimeMs_;
        const float targetFPS = snapshotTargetFps_;

        // FPS ヘッダー表示（Unity風：大きな数字）
        ImGui::PushStyleColor(ImGuiCol_Text, GetFpsColor(currentFPS));
        ImGui::SetWindowFontScale(1.4f);
        ImGui::Text("%.1f FPS", currentFPS);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
        ImGui::TextColored(kLabelColor, "/ %.0f  |  %.3f ms", targetFPS, deltaTimeMs);

        ImGui::Spacing();

        // FPSグラフ（直近120フレーム）
        ImGui::PushStyleColor(ImGuiCol_PlotLines, GetFpsColor(currentFPS));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.10f, 0.10f, 0.10f, 1.0f));
        ImGui::PlotLines(
            "##fps_graph",
            fpsHistory_,
            kFpsHistorySize,
            fpsHistoryIndex_,
            nullptr,
            0.0f,
            targetFPS * 1.2f,
            ImVec2(-1.0f, 72.0f));
        ImGui::PopStyleColor(2);

        ImGui::Spacing();
        ImGui::TextColored(kHeaderColor, "  フレーム統計");
        ImGui::Separator();
        ImGui::Spacing();

        // テーブル行背景色をUnity風に設定
        ImGui::PushStyleColor(ImGuiCol_TableRowBg, ImVec4(0.13f, 0.13f, 0.13f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(0.16f, 0.16f, 0.16f, 1.0f));
        if (ImGui::BeginTable("perf_table", 2, kTableFlags))
        {
            ImGui::TableSetupColumn("項目", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("値", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            DrawKeyValue("目標FPS", "%.0f", targetFPS);
            DrawKeyValue("現在のFPS", "%.2f", currentFPS);
            DrawKeyValue("フレーム時間", "%.3f ms", deltaTimeMs);
            ImGui::EndTable();
        }
        ImGui::PopStyleColor(2);

        // GPU フレーム内訳セクション
        DrawGpuTimingsSection();

        // 統計計測・CSV 出力セクション
        DrawMeasurementSection();
    }

    void EngineStatsWindow::DrawRenderingTab()
    {
        const auto& rs = snapshotRender_;

        ImGui::TextColored(kHeaderColor, "  ドローコール");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_TableRowBg, ImVec4(0.13f, 0.13f, 0.13f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(0.16f, 0.16f, 0.16f, 1.0f));
        if (ImGui::BeginTable("render_table", 2, kTableFlags))
        {
            ImGui::TableSetupColumn("項目", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("値", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            DrawKeyValue("総ドローコール数", "%u", rs.GetTotalDrawCalls());
            DrawSubKeyValue("通常ドローコール", "%u", rs.drawCallCount);
            DrawSubKeyValue("インスタンシング", "%u", rs.instancedDrawCallCount);
            ImGui::EndTable();
        }
        ImGui::PopStyleColor(2);

        ImGui::Spacing();
        ImGui::TextColored(kHeaderColor, "  バッチ / インスタンス");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_TableRowBg, ImVec4(0.13f, 0.13f, 0.13f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(0.16f, 0.16f, 0.16f, 1.0f));
        if (ImGui::BeginTable("render_table2", 2, kTableFlags))
        {
            ImGui::TableSetupColumn("項目", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("値", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            DrawKeyValue("バッチ数", "%u", rs.batchCount);
            DrawKeyValue("総インスタンス数", "%u", rs.totalInstanceCount);
            DrawKeyValue("平均インスタンス数/バッチ", "%.2f", rs.GetAverageInstancesPerBatch());
            ImGui::EndTable();
        }
        ImGui::PopStyleColor(2);

        ImGui::Spacing();
        ImGui::TextColored(kHeaderColor, "  最適化トグル");
        ImGui::Separator();
        ImGui::Spacing();

        {
            // 各最適化を単独でオン/オフして問題を切り分けるためのトグル群。
            // Hi-Z はGPUリソース状態と結びつくため下の専用セクションで切り替える。
            auto& opt = RenderOptimizationSettings::Get();
            ImGui::Checkbox("視錐台カリング##opt_frustum", &opt.frustumCullingEnabled);
            ImGui::SameLine(0.0f, 16.0f);
            ImGui::Checkbox("LOD##opt_lod", &opt.lodEnabled);

            static constexpr const char* kForcedLodNames[] = { "自動", "LOD0", "LOD1", "LOD2" };
            int forcedIdx = opt.forcedLodIndex + 1;
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::Combo("強制LOD##opt_forced_lod", &forcedIdx, kForcedLodNames, 4)) {
                opt.forcedLodIndex = forcedIdx - 1;
            }
        }

        ImGui::Spacing();
        ImGui::TextColored(kHeaderColor, "  LODレベル別インスタンス数");
        ImGui::Separator();
        ImGui::Spacing();

        {
            const uint32_t lodTotal = rs.lodInstanceCounts[0] + rs.lodInstanceCounts[1] + rs.lodInstanceCounts[2];
            const float lodBarMax = (std::max)(lodTotal, 1u) * 1.0f;
            static constexpr const char* kLodLabels[3] = { "LOD0 (フル詳細)", "LOD1 (25%)", "LOD2 (6%)" };
            static constexpr ImVec4 kLodColors[3] = {
                ImVec4(0.25f, 0.75f, 0.35f, 0.85f), // 緑
                ImVec4(0.85f, 0.70f, 0.15f, 0.85f), // 黄
                ImVec4(0.90f, 0.30f, 0.25f, 0.85f), // 赤
            };

            ImGui::PushStyleColor(ImGuiCol_TableRowBg, ImVec4(0.13f, 0.13f, 0.13f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(0.16f, 0.16f, 0.16f, 1.0f));
            if (ImGui::BeginTable("lod_table", 3,
                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX))
            {
                ImGui::TableSetupColumn("レベル", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableSetupColumn("インスタンス数", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn("比率", ImGuiTableColumnFlags_WidthStretch);

                for (int i = 0; i < 3; ++i)
                {
                    const uint32_t count = rs.lodInstanceCounts[i];
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextColored(kLabelColor, "%s", kLodLabels[i]);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextColored(kValueColor, "%u", count);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, kLodColors[i]);
                    ImGui::ProgressBar(static_cast<float>(count) / lodBarMax, ImVec2(-1.0f, 0.0f), "");
                    ImGui::PopStyleColor();
                }
                ImGui::EndTable();
            }
            ImGui::PopStyleColor(2);
        }

        ImGui::Spacing();
        ImGui::TextColored(kHeaderColor, "  Hi-Z オクルージョンカリング");
        ImGui::Separator();
        ImGui::Spacing();

        {
            if (HiZOcclusionSystem* hiZ = engine_ ? engine_->GetHiZOcclusionSystem() : nullptr)
            {
                bool hiZEnabled = hiZ->IsEnabled();
                if (ImGui::Checkbox("有効##hiz_occlusion", &hiZEnabled))
                {
                    hiZ->SetEnabled(hiZEnabled);
                }
            }

            ImGui::PushStyleColor(ImGuiCol_TableRowBg, ImVec4(0.13f, 0.13f, 0.13f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(0.16f, 0.16f, 0.16f, 1.0f));
            if (ImGui::BeginTable("hiz_table", 2, kTableFlags))
            {
                ImGui::TableSetupColumn("項目", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("値", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                DrawKeyValue("判定対象数(サブメッシュ)", "%u", rs.occlusionTestedCount);
                DrawKeyValue("遮蔽スキップ数", "%u", rs.occlusionCulledCount);
                const float cullRate = rs.occlusionTestedCount > 0
                    ? static_cast<float>(rs.occlusionCulledCount) / rs.occlusionTestedCount * 100.0f
                    : 0.0f;
                DrawKeyValue("カリング率", "%.1f%%", cullRate);
                ImGui::EndTable();
            }
            ImGui::PopStyleColor(2);
        }

        ImGui::Spacing();
        ImGui::TextColored(kHeaderColor, "  ジオメトリ");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_TableRowBg, ImVec4(0.13f, 0.13f, 0.13f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(0.16f, 0.16f, 0.16f, 1.0f));
        if (ImGui::BeginTable("render_table3", 2, kTableFlags))
        {
            ImGui::TableSetupColumn("項目", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("値", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            DrawKeyValue("描画三角形数", "%u", rs.totalTriangles);
            DrawKeyValue("描画頂点数", "%u", rs.totalVertices);
            ImGui::EndTable();
        }
        ImGui::PopStyleColor(2);

        ImGui::Spacing();
        ImGui::TextColored(kSubLabel, "※ 統計はフレームごとにリセットされます");

        // パス別 GPU/CPU コスト
        DrawPassTimingsSection();
    }

    void EngineStatsWindow::DrawGpuTimingsSection()
    {
        if (!gpuProfiler_ || !gpuProfiler_->IsInitialized()) return;

        ImGui::Spacing();
        ImGui::TextColored(kHeaderColor, "  GPU フレーム内訳");
        ImGui::Separator();
        ImGui::Spacing();

        const uint32_t totalSlot = static_cast<uint32_t>(GpuTimestampSlot::Total);
        const float gpuTotalMs = snapshotGpu_[totalSlot].gpuMs;

        // GPU フレーム合計グラフ
        ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.3f, 0.8f, 1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.10f, 0.10f, 0.10f, 1.0f));
        char gpuOverlay[32];
        snprintf(gpuOverlay, sizeof(gpuOverlay), "%.2f ms", gpuTotalMs);
        ImGui::PlotLines("##gpu_graph",
            gpuTotalHistory_, kGpuHistorySize, gpuHistoryIndex_,
            gpuOverlay, 0.0f, 33.3f, ImVec2(-1.0f, 48.0f));
        ImGui::PopStyleColor(2);

        ImGui::Spacing();

        // パス別横棒グラフ（合計に対する割合でビジュアル化）
        const float barMaxMs = (std::max)(gpuTotalMs, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_TableRowBg, ImVec4(0.13f, 0.13f, 0.13f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(0.16f, 0.16f, 0.16f, 1.0f));
        if (ImGui::BeginTable("gpu_timing_table", 3,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX))
        {
            ImGui::TableSetupColumn("パス", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthFixed, 52.0f);
            ImGui::TableSetupColumn("比率", ImGuiTableColumnFlags_WidthStretch);

            // カテゴリ別に表示（パス名から解決した GpuTimingCategory でグルーピング。
            // RenderGraph に新規パスを追加してもここは編集不要）
            const std::vector<GpuTimingGroup> groups = BuildGpuTimingGroups(frozenGpu_);

            for (const auto& group : groups)
            {
                // カテゴリヘッダー行
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(kHeaderColor, "%s", GpuTimestampProfiler::GetCategoryLabel(group.category));

                for (uint32_t idx : group.slotIndices)
                {
                    const auto& r = frozenGpu_[idx];

                    // 今フレーム走らなかったパスも行は残す（行の出入りで表全体がずれないように）。
                    // 走っていないことは淡色で示す。
                    const bool idle = IsIdleTimingSlot(r);

                    float ratio = r.gpuMs / barMaxMs;
                    ImVec4 barColor = ratio < 0.3f ? ImVec4(0.25f, 0.75f, 0.35f, 0.85f) :
                        ratio < 0.6f ? ImVec4(0.85f, 0.70f, 0.15f, 0.85f) :
                        ImVec4(0.90f, 0.30f, 0.25f, 0.85f);

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Indent(12.0f);
                    ImGui::TextColored(idle ? kIdleColor : kLabelColor, "%s", r.name);
                    ImGui::Unindent(12.0f);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextColored(idle ? kIdleColor : kValueColor, "%.3f", r.gpuMs);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barColor);
                    ImGui::ProgressBar(ratio, ImVec2(-1.0f, 0.0f), "");
                    ImGui::PopStyleColor();
                }
            }

            // Total 行
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(kHeaderColor, "合計");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(kHeaderColor, "%.3f", gpuTotalMs);

            ImGui::EndTable();
        }
        ImGui::PopStyleColor(2);
    }

    void EngineStatsWindow::DrawMeasurementSection()
    {
        if (!gpuProfiler_ || !gpuProfiler_->IsInitialized()) return;

        ImGui::Spacing();
        ImGui::TextColored(kHeaderColor, "  計測キャプチャ（統計 / CSV 出力）");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextColored(kSubLabel,
            "N フレーム収集して中央値・p95 を出す。瞬間値ではなく、この値を資料へ載せる。");
        ImGui::Spacing();

        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputInt("ウォームアップ frames##cap_warmup", &captureWarmupFrames_, 10, 60);
        captureWarmupFrames_ = (std::max)(0, captureWarmupFrames_);

        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputInt("収集 frames##cap_frames", &captureFrameCount_, 30, 120);
        captureFrameCount_ = std::clamp(captureFrameCount_, 1,
            static_cast<int>(GpuTimingStatsCollector::kMaxCaptureFrames));

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##cap_label", "条件名（例: FFT_N256_CausticsRT）",
            captureLabel_, sizeof(captureLabel_));
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##cap_note", "備考（風速・太陽高度・カメラ構図など）",
            captureNote_, sizeof(captureNote_));

        ImGui::Spacing();

        if (timingCapture_.IsCapturing())
        {
            char overlay[64];
            if (timingCapture_.IsWarmingUp())
            {
                snprintf(overlay, sizeof(overlay), "ウォームアップ中 残り %u",
                    timingCapture_.GetRemainingWarmupFrames());
            }
            else
            {
                snprintf(overlay, sizeof(overlay), "収集中 %u / %u",
                    timingCapture_.GetCapturedFrameCount(), timingCapture_.GetTargetFrameCount());
            }
            ImGui::ProgressBar(timingCapture_.GetProgress01(), ImVec2(-1.0f, 0.0f), overlay);
            if (ImGui::Button("中断##cap_cancel"))
            {
                timingCapture_.Cancel();
            }
        }
        else
        {
            if (ImGui::Button("計測開始##cap_start", ImVec2(120.0f, 0.0f)))
            {
                timingCapture_.Start(
                    static_cast<uint32_t>(captureWarmupFrames_),
                    static_cast<uint32_t>(captureFrameCount_));
                lastExportPath_.clear();
            }
            ImGui::SameLine();
            ImGui::TextColored(kSubLabel, "※ 計測中はカメラ・設定を触らないこと");
        }

        if (!timingCapture_.HasResult())
        {
            return;
        }

        ImGui::Spacing();
        ImGui::TextColored(kHeaderColor, "  計測結果");
        ImGui::Separator();
        ImGui::Spacing();

        // フレーム全体・カテゴリ合計・パス別を 1 つの表にまとめる。
        // 「中央値 / p95」の 2 列で、代表値とばらつきを同時に読めるようにする。
        auto drawSummaryRow = [](const GpuTimingSummary& s, bool indent, const char* unit) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (indent) ImGui::Indent(12.0f);
            ImGui::TextColored(indent ? kSubLabel : kLabelColor, "%s", s.name.c_str());
            if (indent) ImGui::Unindent(12.0f);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(kValueColor, "%.3f %s", s.gpuMedianMs, unit);
            ImGui::TableSetColumnIndex(2);
            ImGui::TextColored(kSubLabel, "%.3f", s.gpuP95Ms);
            ImGui::TableSetColumnIndex(3);
            ImGui::TextColored(kSubLabel, "%.3f", s.gpuMeanMs);
        };

        ImGui::PushStyleColor(ImGuiCol_TableRowBg, ImVec4(0.13f, 0.13f, 0.13f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(0.16f, 0.16f, 0.16f, 1.0f));
        if (ImGui::BeginTable("capture_result_table", 4,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_BordersOuter))
        {
            ImGui::TableSetupColumn("項目", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("中央値", ImGuiTableColumnFlags_WidthFixed, 78.0f);
            ImGui::TableSetupColumn("p95", ImGuiTableColumnFlags_WidthFixed, 58.0f);
            ImGui::TableSetupColumn("平均", ImGuiTableColumnFlags_WidthFixed, 58.0f);
            ImGui::TableHeadersRow();

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(kHeaderColor, "フレーム");
            drawSummaryRow(timingCapture_.GetFrameGpuSummary(), true, "ms");
            drawSummaryRow(timingCapture_.GetFrameCpuSummary(), true, "ms");
            drawSummaryRow(timingCapture_.GetFpsSummary(), true, "fps");
            drawSummaryRow(timingCapture_.GetWaterSharePercent(), true, "%");
            drawSummaryRow(timingCapture_.GetAuxViewSummary(), true, "ms");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(kHeaderColor, "カテゴリ合計");
            for (const auto& s : timingCapture_.GetCategorySummaries())
            {
                drawSummaryRow(s, true, "ms");
            }

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(kHeaderColor, "パス別（中央値の降順）");
            for (const auto& s : timingCapture_.GetSummaries())
            {
                drawSummaryRow(s, true, "ms");
            }

            ImGui::EndTable();
        }
        ImGui::PopStyleColor(2);

        ImGui::Spacing();
        if (ImGui::Button("CSV 出力##cap_export", ImVec2(120.0f, 0.0f)))
        {
            GpuTimingCaptureMeta meta;
            meta.label = captureLabel_;
            meta.note = captureNote_;
            meta.gpuName = gpuName_;
#if defined(_DEBUG)
            meta.buildConfig = "Debug";
#elif defined(NDEBUG)
            meta.buildConfig = "Release";
#else
            meta.buildConfig = "Development";
#endif
            if (engine_)
            {
                if (auto* dx = engine_->GetService<GraphicsCore>())
                {
                    meta.widthPixels = static_cast<uint32_t>(dx->GetClientWidth());
                    meta.heightPixels = static_cast<uint32_t>(dx->GetClientHeight());
                }
            }

            std::string exported;
            if (timingCapture_.ExportCsv("Captures\\Profiling", meta, exported))
            {
                lastExportPath_ = exported;
            }
        }
        if (!lastExportPath_.empty())
        {
            ImGui::SameLine();
            ImGui::TextColored(kSubLabel, "出力: %s", lastExportPath_.c_str());
        }

        ImGui::Spacing();
        ImGui::TextColored(kSubLabel,
            "※ Water 占有率は Water カテゴリ / フレーム合計。FFT 内訳（Water Sim）は");
        ImGui::TextColored(kSubLabel,
            "   FFTOceanPass の内側なので、二重に足さないこと。");
    }

    void EngineStatsWindow::DrawPassTimingsSection()
    {
        if (!gpuProfiler_ || !gpuProfiler_->IsInitialized()) return;

        ImGui::Spacing();
        ImGui::TextColored(kHeaderColor, "  パス別 GPU / CPU コスト");
        ImGui::Separator();
        ImGui::Spacing();

        // カテゴリ別に表示（パス名から解決した GpuTimingCategory でグルーピング。
        // RenderGraph に新規パスを追加してもここは編集不要）
        const std::vector<GpuTimingGroup> groups = BuildGpuTimingGroups(frozenGpu_);

        ImGui::PushStyleColor(ImGuiCol_TableRowBg, ImVec4(0.13f, 0.13f, 0.13f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(0.16f, 0.16f, 0.16f, 1.0f));
        if (ImGui::BeginTable("pass_timing_table", 3,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_BordersOuter))
        {
            ImGui::TableSetupColumn("パス", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("GPU ms", ImGuiTableColumnFlags_WidthFixed, 62.0f);
            ImGui::TableSetupColumn("CPU ms", ImGuiTableColumnFlags_WidthFixed, 62.0f);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            for (const auto& group : groups)
            {
                // カテゴリヘッダー行
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(kHeaderColor, "%s", GpuTimestampProfiler::GetCategoryLabel(group.category));

                for (uint32_t idx : group.slotIndices)
                {
                    const auto& r = frozenGpu_[idx];
                    const bool idle = IsIdleTimingSlot(r);

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Indent(12.0f);
                    ImGui::TextColored(idle ? kIdleColor : kLabelColor, "%s", r.name);
                    ImGui::Unindent(12.0f);

                    ImGui::TableSetColumnIndex(1);
                    ImVec4 gpuColor = idle ? kIdleColor :
                        r.gpuMs > 5.0f ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f) :
                        r.gpuMs > 2.0f ? ImVec4(1.0f, 0.8f, 0.2f, 1.0f) :
                        kValueColor;
                    ImGui::TextColored(gpuColor, "%.3f", r.gpuMs);

                    ImGui::TableSetColumnIndex(2);
                    ImVec4 cpuColor = idle ? kIdleColor :
                        r.cpuMs > 3.0f ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f) :
                        r.cpuMs > 1.0f ? ImVec4(1.0f, 0.8f, 0.2f, 1.0f) :
                        kSubLabel;
                    ImGui::TextColored(cpuColor, "%.3f", r.cpuMs);
                }
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleColor(2);

        ImGui::Spacing();
        ImGui::TextColored(kSubLabel, "※ GPU 値は 1 フレーム遅延あり / 表示は %.1f 秒間隔で更新", kTimingUpdateInterval);
        ImGui::TextColored(kSubLabel, "※ 淡色の行は、そのフレームで実質的に実行されなかったパス（行は固定して表示）");
    }

    void EngineStatsWindow::DrawSceneTab()
    {
        const auto& ss = snapshotScene_;

        ImGui::TextColored(kHeaderColor, "  シーン統計");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_TableRowBg, ImVec4(0.13f, 0.13f, 0.13f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(0.16f, 0.16f, 0.16f, 1.0f));
        if (ImGui::BeginTable("scene_table", 2, kTableFlags))
        {
            ImGui::TableSetupColumn("項目", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("値", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            DrawKeyValue("GameObject総数", "%u", ss.totalGameObjectCount);
            DrawKeyValue("モデルインスタンス数", "%u", ss.modelInstanceCount);
            DrawSubKeyValue("静的モデル", "%u", ss.staticModelCount);
            DrawSubKeyValue("キーフレームモデル", "%u", ss.keyframeModelCount);
            DrawSubKeyValue("スケルトンモデル", "%u", ss.skeletonModelCount);
            DrawKeyValue("ライト数", "%u", ss.lightCount);
            DrawKeyValue("再生中アニメーション数", "%u", ss.playingAnimationCount);
            ImGui::EndTable();
        }
        ImGui::PopStyleColor(2);
    }

    void EngineStatsWindow::DrawResourceTab()
    {
        const auto& cs = snapshotResource_;

        ImGui::TextColored(kHeaderColor, "  リソースキャッシュ");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_TableRowBg, ImVec4(0.13f, 0.13f, 0.13f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(0.16f, 0.16f, 0.16f, 1.0f));
        if (ImGui::BeginTable("res_table", 2, kTableFlags))
        {
            ImGui::TableSetupColumn("項目", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("値", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            DrawKeyValue("ロード済みモデル数", "%u", cs.loadedModelCount);
            DrawKeyValue("ロード中リソース数", "%u", cs.loadingResourceCount);
            DrawKeyValue("キャッシュヒット数", "%u", cs.cacheHitCount);
            DrawKeyValue("キャッシュミス数", "%u", cs.cacheMissCount);
            DrawKeyValue("キャッシュヒット率", "%.1f%%", cs.GetCacheHitRate());
            ImGui::EndTable();
        }
        ImGui::PopStyleColor(2);

        ImGui::Spacing();
        // ヒット率プログレスバー
        float hitRate = cs.GetCacheHitRate() / 100.0f;
        ImGui::TextColored(kLabelColor, "ヒット率");
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
            hitRate >= 0.8f ? ImVec4(0.2f, 0.85f, 0.3f, 1.0f) :
            hitRate >= 0.5f ? ImVec4(1.0f, 0.8f, 0.0f, 1.0f) :
            ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        ImGui::ProgressBar(hitRate, ImVec2(-1.0f, 0.0f));
        ImGui::PopStyleColor();
    }

    void EngineStatsWindow::DrawMemoryTab()
    {
        const auto& ms = snapshotMemory_;
        char buf[64];

        ImGui::PushStyleColor(ImGuiCol_TableRowBg, ImVec4(0.13f, 0.13f, 0.13f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(0.16f, 0.16f, 0.16f, 1.0f));

        ImGui::TextColored(kHeaderColor, "  GPU メモリ");
        ImGui::Separator();
        ImGui::Spacing();
        if (ImGui::BeginTable("mem_gpu_table", 2, kTableFlags))
        {
            ImGui::TableSetupColumn("項目", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("値", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            DrawKeyValue("使用量", "%s", FormatBytes(ms.gpuMemoryUsageBytes, buf, sizeof(buf)));
            DrawKeyValue("予算", "%s", FormatBytes(ms.gpuMemoryBudgetBytes, buf, sizeof(buf)));
            ImGui::EndTable();
        }
        // GPU 使用率バー
        float gpuRatio = ms.gpuMemoryBudgetBytes > 0
            ? static_cast<float>(ms.gpuMemoryUsageBytes) / static_cast<float>(ms.gpuMemoryBudgetBytes)
            : 0.0f;
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
            gpuRatio >= 0.85f ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f) :
            gpuRatio >= 0.60f ? ImVec4(1.0f, 0.8f, 0.0f, 1.0f) :
            ImVec4(0.2f, 0.85f, 0.3f, 1.0f));
        ImGui::ProgressBar(gpuRatio, ImVec2(-1.0f, 0.0f));
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::TextColored(kHeaderColor, "  CPU メモリ");
        ImGui::Separator();
        ImGui::Spacing();
        if (ImGui::BeginTable("mem_cpu_table", 2, kTableFlags))
        {
            ImGui::TableSetupColumn("項目", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("値", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            DrawKeyValue("ワーキングセット", "%s", FormatBytes(ms.cpuWorkingSetBytes, buf, sizeof(buf)));
            DrawKeyValue("プライベートメモリ", "%s", FormatBytes(ms.cpuPrivateBytes, buf, sizeof(buf)));
            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::TextColored(kHeaderColor, "  モデルバッファ（推定）");
        ImGui::Separator();
        ImGui::Spacing();
        if (ImGui::BeginTable("mem_model_table", 2, kTableFlags))
        {
            ImGui::TableSetupColumn("項目", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("値", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            DrawKeyValue("頂点バッファ合計", "%s", FormatBytes(ms.modelVertexBufferBytes, buf, sizeof(buf)));
            DrawKeyValue("インデックスバッファ合計", "%s", FormatBytes(ms.modelIndexBufferBytes, buf, sizeof(buf)));
            ImGui::EndTable();
        }

        ImGui::PopStyleColor(2);
    }

    void EngineStatsWindow::CollectSceneStats()
    {
        auto& ss = EngineStats::GetInstance().GetSceneStats();
        ss.Reset();

        if (!engine_)
        {
            return;
        }

        auto* sceneManager = engine_->GetSceneManager();
        if (!sceneManager)
        {
            return;
        }

        auto* objManager = sceneManager->GetCurrentGameObjectManager();
        if (objManager)
        {
            ss.totalGameObjectCount = static_cast<uint32_t>(objManager->GetObjectCount());
        }

        if (modelManager_)
        {
            // ロード済みモデル数 ≒ 一意なモデルリソース数
            ss.modelInstanceCount = EngineStats::GetInstance().GetResourceCacheStats().loadedModelCount;
        }
    }

    void EngineStatsWindow::CollectMemoryStats()
    {
        auto& ms = EngineStats::GetInstance().GetMemoryStats();

        // CPU メモリ：プロセスのワーキングセット / プライベートメモリ
        PROCESS_MEMORY_COUNTERS_EX pmc{};
        if (GetProcessMemoryInfo(GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
        {
            ms.cpuWorkingSetBytes = pmc.WorkingSetSize;
            ms.cpuPrivateBytes = pmc.PrivateUsage;
        }

        // GPU メモリ：DXGI Adapter3 経由
        if (engine_)
        {
            if (auto* dx = engine_->GetService<GraphicsCore>())
            {
                if (auto* device = dx->GetDevice())
                {
                    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
                    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
                    {
                        LUID luid = device->GetAdapterLuid();
                        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter1;
                        if (SUCCEEDED(factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter1))))
                        {
                            // GPU 名は計測 CSV のメタ情報として一度だけ拾っておく
                            // （どの GPU で測った数値かを数値と同じファイルへ残すため）。
                            if (gpuName_.empty())
                            {
                                DXGI_ADAPTER_DESC1 adapterDesc{};
                                if (SUCCEEDED(adapter1->GetDesc1(&adapterDesc)))
                                {
                                    const int needed = WideCharToMultiByte(
                                        CP_UTF8, 0, adapterDesc.Description, -1, nullptr, 0, nullptr, nullptr);
                                    if (needed > 1)
                                    {
                                        std::string utf8(static_cast<size_t>(needed - 1), '\0');
                                        WideCharToMultiByte(CP_UTF8, 0, adapterDesc.Description, -1,
                                            utf8.data(), needed, nullptr, nullptr);
                                        gpuName_ = utf8;
                                    }
                                }
                            }

                            Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter3;
                            if (SUCCEEDED(adapter1.As(&adapter3)))
                            {
                                DXGI_QUERY_VIDEO_MEMORY_INFO info{};
                                if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(0,
                                    DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info)))
                                {
                                    ms.gpuMemoryUsageBytes = info.CurrentUsage;
                                    ms.gpuMemoryBudgetBytes = info.Budget;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

