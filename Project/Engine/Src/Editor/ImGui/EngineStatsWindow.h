#pragma once
#include "Diagnostics/EngineStats.h"
#include "Graphics/RHI/Debug/GpuTimestampProfiler.h"
#include "Diagnostics/GpuTimingStatsCollector.h"
#include <array>
#include <string>

namespace CoreEngine
{
    class ModelManager;
    class EngineSystem;

    /// @brief FPS・GPU タイミング・メモリなどの統計をまとめて表示するウィンドウ
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
        // 統計計測・CSV 出力（パフォーマンスタブ内部）
        void DrawMeasurementSection();

        /// @brief CVar 指定の経過秒で計測キャプチャを開始し、完了したら CSV を書き出す
        void TickAutoCapture();

        /// @brief 現在の計測結果を条件メタ付きで CSV へ書き出す
        bool ExportTimingCsv(const std::string& label, std::string& outPath);

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

        // パス別タイミング表示の更新インターバル制御
        static constexpr float kTimingUpdateInterval = 0.5f; // 秒
        float timingAccumTime_ = 0.0f;
        std::array<GpuTimingResult, GpuTimestampProfiler::kSlotCount> frozenGpu_ = {};

        // ── 統計計測（N フレーム収集して中央値 / p95 を出し CSV へ書き出す）──
        GpuTimingStatsCollector timingCapture_;
        int captureWarmupFrames_ = static_cast<int>(GpuTimingStatsCollector::kDefaultWarmupFrames);
        int captureFrameCount_ = static_cast<int>(GpuTimingStatsCollector::kDefaultCaptureFrames);
        char captureLabel_[96] = {};  ///< 条件名（A/B の識別に使う）
        char captureNote_[192] = {};  ///< 風速・太陽高度・構図などの自由記入
        std::string lastExportPath_;  ///< 直近に書き出した CSV のパス（UI 表示用）
        std::string gpuName_;         ///< DXGI アダプタ名（CSV のメタ情報）

        // 自動キャプチャ（1 起動につき 1 回だけ走る）
        float autoCaptureElapsedSec_ = 0.0f;
        bool autoCaptureStarted_ = false;
        bool autoCaptureDone_ = false;
    };
}
