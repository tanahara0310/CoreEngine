#include "pch.h"
#include "EngineStatsWindow.h"

#include "Graphics/Debug/EngineStats.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/Common/DirectXCommon.h"
#include "EngineSystem/EngineSystem.h"
#include "Utility/FrameRate/FrameRateController.h"
#include "Scene/SceneManager.h"
#include "ObjectCommon/GameObjectManager.h"

#include <imgui.h>

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
            if (auto* fc = engine_->GetComponent<FrameRateController>())
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

            // カテゴリ別に表示
            struct SlotGroup {
                const char* category;
                GpuTimestampSlot slots[4];
                int count;
            };
            static constexpr SlotGroup kGroups[] = {
                { "Shadow",   { GpuTimestampSlot::ShadowPass, GpuTimestampSlot::RTShadow },                              2 },
                { "G-Buffer", { GpuTimestampSlot::GBufferPass },                                                          1 },
                { "SSAO",     { GpuTimestampSlot::SSAOPass },                                                             1 },
                { "Lighting", { GpuTimestampSlot::DeferredLighting },                                                     1 },
                { "Geometry", { GpuTimestampSlot::GeometryPass },                                                         1 },
                { "PostFX",   { GpuTimestampSlot::PostEffect },                                                           1 },
                { "Editor",   { GpuTimestampSlot::BackBufferPass, GpuTimestampSlot::ImGuiDraw }, 2 },
            };

            for (const auto& group : kGroups)
            {
                bool hasAny = false;
                for (int gi = 0; gi < group.count; ++gi)
                {
                    const auto& r = frozenGpu_[static_cast<uint32_t>(group.slots[gi])];
                    if (r.gpuMs >= 0.001f) { hasAny = true; break; }
                }
                if (!hasAny) continue;

                // カテゴリヘッダー行
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(kHeaderColor, "%s", group.category);

                for (int gi = 0; gi < group.count; ++gi)
                {
                    const auto slot = group.slots[gi];
                    const auto& r = frozenGpu_[static_cast<uint32_t>(slot)];
                    if (r.gpuMs < 0.001f) continue;

                    float ratio = r.gpuMs / barMaxMs;
                    ImVec4 barColor = ratio < 0.3f ? ImVec4(0.25f, 0.75f, 0.35f, 0.85f) :
                        ratio < 0.6f ? ImVec4(0.85f, 0.70f, 0.15f, 0.85f) :
                        ImVec4(0.90f, 0.30f, 0.25f, 0.85f);

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Indent(12.0f);
                    ImGui::TextColored(kLabelColor, "%s", GpuTimestampProfiler::GetSlotName(slot));
                    ImGui::Unindent(12.0f);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextColored(kValueColor, "%.3f", r.gpuMs);
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

    void EngineStatsWindow::DrawPassTimingsSection()
    {
        if (!gpuProfiler_ || !gpuProfiler_->IsInitialized()) return;

        ImGui::Spacing();
        ImGui::TextColored(kHeaderColor, "  パス別 GPU / CPU コスト");
        ImGui::Separator();
        ImGui::Spacing();

        // カテゴリ定義（DrawGpuTimingsSection と同じ構造）
        struct SlotGroup {
            const char* category;
            GpuTimestampSlot slots[4];
            int count;
        };
        static constexpr SlotGroup kGroups[] = {
            { "Shadow",   { GpuTimestampSlot::ShadowPass, GpuTimestampSlot::RTShadow },                                   2 },
            { "G-Buffer", { GpuTimestampSlot::GBufferPass },                                                               1 },
            { "SSAO",     { GpuTimestampSlot::SSAOPass },                                                                  1 },
            { "Lighting", { GpuTimestampSlot::DeferredLighting },                                                          1 },
            { "Geometry", { GpuTimestampSlot::GeometryPass },                                                              1 },
            { "PostFX",   { GpuTimestampSlot::PostEffect },                                                                1 },
            { "Editor",   { GpuTimestampSlot::BackBufferPass, GpuTimestampSlot::ImGuiDraw },  2 },
        };

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

            for (const auto& group : kGroups)
            {
                bool hasAny = false;
                for (int gi = 0; gi < group.count; ++gi)
                {
                    const auto& r = frozenGpu_[static_cast<uint32_t>(group.slots[gi])];
                    if (r.gpuMs >= 0.001f || r.cpuMs >= 0.001f) { hasAny = true; break; }
                }
                if (!hasAny) continue;

                // カテゴリヘッダー行
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(kHeaderColor, "%s", group.category);

                for (int gi = 0; gi < group.count; ++gi)
                {
                    const auto slot = group.slots[gi];
                    const uint32_t idx = static_cast<uint32_t>(slot);
                    const auto& r = frozenGpu_[idx];
                    if (r.gpuMs < 0.001f && r.cpuMs < 0.001f) continue;

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Indent(12.0f);
                    ImGui::TextColored(kLabelColor, "%s", r.name[0] ? r.name : GpuTimestampProfiler::GetSlotName(slot));
                    ImGui::Unindent(12.0f);

                    ImGui::TableSetColumnIndex(1);
                    ImVec4 gpuColor = r.gpuMs > 5.0f ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f) :
                        r.gpuMs > 2.0f ? ImVec4(1.0f, 0.8f, 0.2f, 1.0f) :
                        kValueColor;
                    ImGui::TextColored(gpuColor, "%.3f", r.gpuMs);

                    ImGui::TableSetColumnIndex(2);
                    ImVec4 cpuColor = r.cpuMs > 3.0f ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f) :
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
            if (auto* dx = engine_->GetComponent<DirectXCommon>())
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

