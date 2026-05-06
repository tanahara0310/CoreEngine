#pragma once
#include "Graphics/Debug/EngineStats.h"
#include "Graphics/Common/GpuTimestampProfiler.h"
#include <array>

namespace CoreEngine
{
    class ModelManager;
    class EngineSystem;

    class EngineStatsWindow
    {
    public:
        EngineStatsWindow() = default;
        ~EngineStatsWindow() = default;

        void SetEngineSystem(EngineSystem* engine) { engine_ = engine; }
        void SetModelManager(ModelManager* manager) { modelManager_ = manager; }
        void SetGpuProfiler(GpuTimestampProfiler* profiler) { gpuProfiler_ = profiler; }

        /// @brief FPS履歴・シーン統計・メモリ統計・レンダリング統計を収集（描画完了後に1回呼ぶ）
        void Collect();

        // 各カテゴリのコンテンツ描画（呼び出し元で ImGui::Begin/End を管理）
        void DrawPerformanceTab();
        void DrawRenderingTab();
        void DrawSceneTab();
        void DrawResourceTab();
        void DrawMemoryTab();

    private:
        void CollectSceneStats();
        void CollectMemoryStats();

        // GPU タイムライングラフ描画（パフォーマンスタブ内部）
        void DrawGpuTimingsSection();
        // パス別コスト描画（レンダリングタブ内部）
        void DrawPassTimingsSection();

    private:
        EngineSystem* engine_ = nullptr;
        ModelManager* modelManager_ = nullptr;
        GpuTimestampProfiler* gpuProfiler_ = nullptr;

        static constexpr int kFpsHistorySize = 120;
        float fpsHistory_[kFpsHistorySize] = {};
        int fpsHistoryIndex_ = 0;

        // GPU タイミング履歴（フレーム合計のグラフ用）
        static constexpr int kGpuHistorySize = 120;
        float gpuTotalHistory_[kGpuHistorySize] = {};
        int gpuHistoryIndex_ = 0;

        // 前フレーム描画完了後のスナップショット（Draw 時はこちらを参照）
        RenderStats       snapshotRender_;
        SceneStats        snapshotScene_;
        ResourceCacheStats snapshotResource_;
        MemoryStats       snapshotMemory_;
        std::array<GpuTimingResult, GpuTimestampProfiler::kSlotCount> snapshotGpu_ = {};
        float snapshotFps_ = 0.0f;
        float snapshotDeltaTimeMs_ = 0.0f;
        float snapshotTargetFps_ = 60.0f;
    };
}
