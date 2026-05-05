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

        void DrawKeyValue(const char* key, const char* fmt, ...)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(key);
            ImGui::TableSetColumnIndex(1);
            va_list args;
            va_start(args, fmt);
            ImGui::TextV(fmt, args);
            va_end(args);
        }

        const char* FormatBytes(uint64_t bytes, char* buf, size_t bufSize)
        {
            const double KB = 1024.0;
            const double MB = KB * 1024.0;
            const double GB = MB * 1024.0;
            if (bytes >= static_cast<uint64_t>(GB))
            {
                snprintf(buf, bufSize, "%.2f GB", static_cast<double>(bytes) / GB);
            }
            else if (bytes >= static_cast<uint64_t>(MB))
            {
                snprintf(buf, bufSize, "%.2f MB", static_cast<double>(bytes) / MB);
            }
            else if (bytes >= static_cast<uint64_t>(KB))
            {
                snprintf(buf, bufSize, "%.2f KB", static_cast<double>(bytes) / KB);
            }
            else
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
                snapshotFps_         = fc->GetCurrentFPS();
                snapshotDeltaTimeMs_ = fc->GetDeltaTime() * 1000.0f;
                snapshotTargetFps_   = fc->GetTargetFPS();
                fpsHistory_[fpsHistoryIndex_] = snapshotFps_;
                fpsHistoryIndex_ = (fpsHistoryIndex_ + 1) % kFpsHistorySize;
            }
        }

        // 描画完了後のスナップショット保存（Draw 時はこちらを参照）
        snapshotRender_   = EngineStats::GetInstance().GetRenderStats();
        snapshotScene_    = EngineStats::GetInstance().GetSceneStats();
        snapshotResource_ = EngineStats::GetInstance().GetResourceCacheStats();
        snapshotMemory_   = EngineStats::GetInstance().GetMemoryStats();
    }

    void EngineStatsWindow::DrawPerformanceTab()
    {
        const float currentFPS  = snapshotFps_;
        const float deltaTimeMs = snapshotDeltaTimeMs_;
        const float targetFPS   = snapshotTargetFps_;

        ImGui::TextColored(GetFpsColor(currentFPS), "FPS: %.1f / %.0f", currentFPS, targetFPS);
        ImGui::SameLine();
        ImGui::Text("   フレーム時間: %.3f ms", deltaTimeMs);

        // FPSグラフ（直近120フレーム）
        ImGui::PlotLines(
            "##fps_graph",
            fpsHistory_,
            kFpsHistorySize,
            fpsHistoryIndex_,
            nullptr,
            0.0f,
            targetFPS * 1.2f,
            ImVec2(-1.0f, 80.0f));

        ImGui::Separator();

        if (ImGui::BeginTable("perf_table", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH))
        {
            DrawKeyValue("目標FPS", "%.0f", targetFPS);
            DrawKeyValue("現在のFPS", "%.2f", currentFPS);
            DrawKeyValue("フレーム時間", "%.3f ms", deltaTimeMs);
            ImGui::EndTable();
        }
    }

    void EngineStatsWindow::DrawRenderingTab()
    {
        const auto& rs = snapshotRender_;

        if (ImGui::BeginTable("render_table", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH))
        {
            DrawKeyValue("総ドローコール数", "%u", rs.GetTotalDrawCalls());
            DrawKeyValue("  通常ドローコール", "%u", rs.drawCallCount);
            DrawKeyValue("  インスタンシングドローコール", "%u", rs.instancedDrawCallCount);
            DrawKeyValue("バッチ数", "%u", rs.batchCount);
            DrawKeyValue("総インスタンス数", "%u", rs.totalInstanceCount);
            DrawKeyValue("平均インスタンス数/バッチ", "%.2f", rs.GetAverageInstancesPerBatch());
            DrawKeyValue("描画三角形数", "%u", rs.totalTriangles);
            DrawKeyValue("描画頂点数", "%u", rs.totalVertices);
            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::TextDisabled("※ 統計はフレームごとにリセットされます");
    }

    void EngineStatsWindow::DrawSceneTab()
    {
        const auto& ss = snapshotScene_;

        if (ImGui::BeginTable("scene_table", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH))
        {
            DrawKeyValue("GameObject総数", "%u", ss.totalGameObjectCount);
            DrawKeyValue("モデルインスタンス数", "%u", ss.modelInstanceCount);
            DrawKeyValue("  静的モデル", "%u", ss.staticModelCount);
            DrawKeyValue("  キーフレームモデル", "%u", ss.keyframeModelCount);
            DrawKeyValue("  スケルトンモデル", "%u", ss.skeletonModelCount);
            DrawKeyValue("ライト数", "%u", ss.lightCount);
            DrawKeyValue("再生中アニメーション数", "%u", ss.playingAnimationCount);
            ImGui::EndTable();
        }
    }

    void EngineStatsWindow::DrawResourceTab()
    {
        const auto& cs = snapshotResource_;

        if (ImGui::BeginTable("res_table", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH))
        {
            DrawKeyValue("ロード済みモデル数", "%u", cs.loadedModelCount);
            DrawKeyValue("ロード中リソース数", "%u", cs.loadingResourceCount);
            DrawKeyValue("キャッシュヒット数", "%u", cs.cacheHitCount);
            DrawKeyValue("キャッシュミス数", "%u", cs.cacheMissCount);
            DrawKeyValue("キャッシュヒット率", "%.1f %%", cs.GetCacheHitRate());
            ImGui::EndTable();
        }
    }

    void EngineStatsWindow::DrawMemoryTab()
    {
        const auto& ms = snapshotMemory_;
        char buf[64];

        ImGui::SeparatorText("GPU メモリ");
        if (ImGui::BeginTable("mem_gpu_table", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH))
        {
            DrawKeyValue("GPU 使用量", "%s", FormatBytes(ms.gpuMemoryUsageBytes, buf, sizeof(buf)));
            DrawKeyValue("GPU 予算", "%s", FormatBytes(ms.gpuMemoryBudgetBytes, buf, sizeof(buf)));
            float ratio = ms.gpuMemoryBudgetBytes > 0
                ? static_cast<float>(ms.gpuMemoryUsageBytes) / static_cast<float>(ms.gpuMemoryBudgetBytes)
                : 0.0f;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("使用率");
            ImGui::TableSetColumnIndex(1);
            ImGui::ProgressBar(ratio, ImVec2(-1.0f, 0.0f));
            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::SeparatorText("CPU メモリ");
        if (ImGui::BeginTable("mem_cpu_table", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH))
        {
            DrawKeyValue("ワーキングセット", "%s", FormatBytes(ms.cpuWorkingSetBytes, buf, sizeof(buf)));
            DrawKeyValue("プライベートメモリ", "%s", FormatBytes(ms.cpuPrivateBytes, buf, sizeof(buf)));
            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::SeparatorText("モデルバッファ（推定）");
        if (ImGui::BeginTable("mem_model_table", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH))
        {
            DrawKeyValue("頂点バッファ合計", "%s", FormatBytes(ms.modelVertexBufferBytes, buf, sizeof(buf)));
            DrawKeyValue("インデックスバッファ合計", "%s", FormatBytes(ms.modelIndexBufferBytes, buf, sizeof(buf)));
            ImGui::EndTable();
        }
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

