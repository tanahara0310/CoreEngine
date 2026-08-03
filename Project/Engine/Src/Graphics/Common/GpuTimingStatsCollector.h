#pragma once

#include "Graphics/Common/GpuTimestampProfiler.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace CoreEngine
{
    /// @brief 1 系列（パス 1 本、またはフレーム全体などの指標 1 本）分の統計
    /// @details 平均だけだと単発スパイクに引きずられるため、中央値と p95 を併記する。
    ///          スライド等へ載せる代表値は中央値、ばらつきの説明は p95 を使う想定。
    struct GpuTimingSummary
    {
        std::string name;
        GpuTimingCategory category = GpuTimingCategory::Setup;
        float gpuMeanMs = 0.0f;
        float gpuMedianMs = 0.0f;
        float gpuP95Ms = 0.0f;
        float gpuMinMs = 0.0f;
        float gpuMaxMs = 0.0f;
        float cpuMeanMs = 0.0f;
        float cpuMedianMs = 0.0f;
        uint32_t sampleCount = 0;
    };

    /// @brief 計測条件のメタ情報（CSV の先頭へ書き出す）
    /// @details 「何を固定して測ったか」を数値と同じファイルへ残さないと、
    ///          後から数値の意味を再構成できなくなるため必須の項目として持つ。
    struct GpuTimingCaptureMeta
    {
        std::string label;       ///< 条件名（"FFT N=256 / Caustics=RT" など）
        std::string gpuName;     ///< GPU 名（アダプタ記述）
        std::string buildConfig; ///< ビルド構成
        uint32_t widthPixels = 0;
        uint32_t heightPixels = 0;
        std::string note;        ///< 自由記入（風速・太陽高度・カメラ構図など）
    };

    /// @brief GpuTimestampProfiler の毎フレーム結果を貯めて統計と CSV を出す収集器
    ///
    /// @details 使い方:
    ///   1. Start(warmupFrames, captureFrames)
    ///   2. 毎フレーム Tick(profiler->GetResults(), cpuFrameMs, fps)
    ///      （warmup 中のフレームは捨てられる。GPU クロックが上がりきる前の値を混ぜないため）
    ///   3. HasResult() が true になったら GetSummaries() / ExportCsv()
    class GpuTimingStatsCollector
    {
    public:
        static constexpr uint32_t kDefaultWarmupFrames = 120;
        static constexpr uint32_t kDefaultCaptureFrames = 300;
        static constexpr uint32_t kMaxCaptureFrames = 3600;

        /// @brief 計測を開始する（進行中の計測と過去の結果は破棄される）
        void Start(uint32_t warmupFrames, uint32_t captureFrames);

        /// @brief 進行中の計測を中断する（結果は残さない）
        void Cancel();

        /// @brief 1 フレーム分のサンプルを投入する
        /// @param results GpuTimestampProfiler::GetResults()（1 フレーム遅延あり）
        /// @param cpuFrameMs CPU 側のフレーム時間（ms）
        /// @param fps 現在の FPS
        void Tick(
            const std::array<GpuTimingResult, GpuTimestampProfiler::kSlotCount>& results,
            float cpuFrameMs,
            float fps);

        bool IsCapturing() const noexcept { return state_ != State::Idle && state_ != State::Finished; }
        bool IsWarmingUp() const noexcept { return state_ == State::Warmup; }
        bool HasResult() const noexcept { return state_ == State::Finished && !summaries_.empty(); }

        /// @brief 進捗 0..1（ウォームアップ込み）
        float GetProgress01() const noexcept;
        uint32_t GetCapturedFrameCount() const noexcept { return capturedFrames_; }
        uint32_t GetTargetFrameCount() const noexcept { return captureFrames_; }
        uint32_t GetRemainingWarmupFrames() const noexcept { return warmupRemaining_; }

        // ── 結果参照 ─────────────────────────────────────────
        /// @brief パス別統計（GPU 中央値の降順）
        const std::vector<GpuTimingSummary>& GetSummaries() const noexcept { return summaries_; }
        /// @brief カテゴリ別統計（毎フレームの合計値に対する統計。「中央値の合計」ではない）
        const std::vector<GpuTimingSummary>& GetCategorySummaries() const noexcept { return categorySummaries_; }

        const GpuTimingSummary& GetFrameGpuSummary() const noexcept { return frameGpu_; }
        const GpuTimingSummary& GetFrameCpuSummary() const noexcept { return frameCpu_; }
        const GpuTimingSummary& GetFpsSummary() const noexcept { return fps_; }
        /// @brief 補助 View（反射ビュー等。スロット名が "View名/パス名"）の合計
        const GpuTimingSummary& GetAuxViewSummary() const noexcept { return auxView_; }

        /// @brief 水面カテゴリがフレーム GPU 時間に占める割合（%）の統計
        /// @details 毎フレームの比率を取ってから統計を出す。合計同士を割ると
        ///          フレーム時間が揺れたときに実態とずれるため。
        const GpuTimingSummary& GetWaterSharePercent() const noexcept { return waterSharePercent_; }

        /// @brief 集計結果と生フレーム値を CSV へ書き出す
        /// @param directory 出力ディレクトリ（存在しなければ作成する）
        /// @param meta 計測条件
        /// @param outSummaryPath 実際に書いた集計 CSV のパス
        /// @return 成功したら true
        bool ExportCsv(
            const std::string& directory,
            const GpuTimingCaptureMeta& meta,
            std::string& outSummaryPath) const;

    private:
        enum class State : uint8_t { Idle, Warmup, Capturing, Finished };

        struct SlotSamples
        {
            std::string name;
            GpuTimingCategory category = GpuTimingCategory::Setup;
            std::vector<float> gpuMs;
            std::vector<float> cpuMs;
            bool everActive = false;
        };

        void Finish();

        State state_ = State::Idle;
        uint32_t warmupRemaining_ = 0;
        uint32_t warmupFrames_ = 0;
        uint32_t captureFrames_ = 0;
        uint32_t capturedFrames_ = 0;

        std::array<SlotSamples, GpuTimestampProfiler::kSlotCount> slots_;
        std::vector<float> frameGpuSamples_;
        std::vector<float> frameCpuSamples_;
        std::vector<float> fpsSamples_;

        std::vector<GpuTimingSummary> summaries_;
        std::vector<GpuTimingSummary> categorySummaries_;
        GpuTimingSummary frameGpu_;
        GpuTimingSummary frameCpu_;
        GpuTimingSummary fps_;
        GpuTimingSummary auxView_;
        GpuTimingSummary waterSharePercent_;
    };
}
