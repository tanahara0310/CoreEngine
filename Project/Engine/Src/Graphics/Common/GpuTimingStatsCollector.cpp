#include "pch.h"
#include "GpuTimingStatsCollector.h"

#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numeric>

namespace CoreEngine
{
    namespace
    {
        float Mean(const std::vector<float>& values)
        {
            if (values.empty()) return 0.0f;
            const double sum = std::accumulate(values.begin(), values.end(), 0.0);
            return static_cast<float>(sum / static_cast<double>(values.size()));
        }

        /// @brief 最近接順位法のパーセンタイル（ソート済み配列に対して）
        float Percentile(const std::vector<float>& sorted, float ratio01)
        {
            if (sorted.empty()) return 0.0f;
            const float position = ratio01 * static_cast<float>(sorted.size() - 1);
            const size_t index = static_cast<size_t>(std::lround(position));
            return sorted[(std::min)(index, sorted.size() - 1)];
        }

        /// @brief GPU 値の系列から統計を作る（CPU 系列は任意）
        GpuTimingSummary MakeSummary(
            const std::string& name,
            GpuTimingCategory category,
            const std::vector<float>& gpuValues,
            const std::vector<float>* cpuValues = nullptr)
        {
            GpuTimingSummary summary;
            summary.name = name;
            summary.category = category;
            summary.sampleCount = static_cast<uint32_t>(gpuValues.size());
            if (gpuValues.empty()) {
                return summary;
            }

            std::vector<float> sorted = gpuValues;
            std::sort(sorted.begin(), sorted.end());

            summary.gpuMeanMs = Mean(gpuValues);
            summary.gpuMedianMs = Percentile(sorted, 0.5f);
            summary.gpuP95Ms = Percentile(sorted, 0.95f);
            summary.gpuMinMs = sorted.front();
            summary.gpuMaxMs = sorted.back();

            if (cpuValues && !cpuValues->empty()) {
                std::vector<float> cpuSorted = *cpuValues;
                std::sort(cpuSorted.begin(), cpuSorted.end());
                summary.cpuMeanMs = Mean(*cpuValues);
                summary.cpuMedianMs = Percentile(cpuSorted, 0.5f);
            }
            return summary;
        }

        /// @brief 補助 View のスロットか（RenderGraph が "View名/パス名" で登録する）
        bool IsAuxViewSlot(const std::string& name)
        {
            return name.find('/') != std::string::npos;
        }

        std::string MakeTimestampString()
        {
            const auto now = std::chrono::system_clock::now();
            const auto time = std::chrono::system_clock::to_time_t(now);
            std::tm localTime{};
            localtime_s(&localTime, &time);

            char buf[32] = {};
            snprintf(buf, sizeof(buf), "%04d%02d%02d_%02d%02d%02d",
                localTime.tm_year + 1900, localTime.tm_mon + 1, localTime.tm_mday,
                localTime.tm_hour, localTime.tm_min, localTime.tm_sec);
            return buf;
        }

        /// @brief ファイル名に使えない文字を潰す
        std::string SanitizeForFileName(const std::string& text)
        {
            std::string result;
            result.reserve(text.size());
            for (const char c : text) {
                const bool invalid =
                    c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' ||
                    c == '"' || c == '<' || c == '>' || c == '|' || c == ' ';
                result.push_back(invalid ? '_' : c);
            }
            if (result.size() > 48) {
                result.resize(48);
            }
            return result;
        }

        /// @brief CSV セル用のエスケープ（カンマ・引用符・改行を含む値を安全にする）
        std::string CsvCell(const std::string& text)
        {
            const bool needsQuote =
                text.find(',') != std::string::npos ||
                text.find('"') != std::string::npos ||
                text.find('\n') != std::string::npos;
            if (!needsQuote) {
                return text;
            }
            std::string escaped = "\"";
            for (const char c : text) {
                if (c == '"') escaped.push_back('"');
                escaped.push_back(c);
            }
            escaped.push_back('"');
            return escaped;
        }
    }

    void GpuTimingStatsCollector::Start(uint32_t warmupFrames, uint32_t captureFrames)
    {
        Cancel();

        warmupFrames_ = warmupFrames;
        warmupRemaining_ = warmupFrames;
        captureFrames_ = (std::clamp)(captureFrames, 1u, kMaxCaptureFrames);
        capturedFrames_ = 0;

        for (auto& slot : slots_) {
            slot.gpuMs.reserve(captureFrames_);
            slot.cpuMs.reserve(captureFrames_);
        }
        frameGpuSamples_.reserve(captureFrames_);
        frameCpuSamples_.reserve(captureFrames_);
        fpsSamples_.reserve(captureFrames_);

        state_ = (warmupFrames_ > 0) ? State::Warmup : State::Capturing;
    }

    void GpuTimingStatsCollector::Cancel()
    {
        state_ = State::Idle;
        warmupRemaining_ = 0;
        capturedFrames_ = 0;

        for (auto& slot : slots_) {
            slot.name.clear();
            slot.category = GpuTimingCategory::Setup;
            slot.gpuMs.clear();
            slot.cpuMs.clear();
            slot.everActive = false;
        }
        frameGpuSamples_.clear();
        frameCpuSamples_.clear();
        fpsSamples_.clear();

        summaries_.clear();
        categorySummaries_.clear();
        frameGpu_ = {};
        frameCpu_ = {};
        fps_ = {};
        auxView_ = {};
        waterSharePercent_ = {};
    }

    float GpuTimingStatsCollector::GetProgress01() const noexcept
    {
        const uint32_t total = warmupFrames_ + captureFrames_;
        if (total == 0) {
            return 0.0f;
        }
        const uint32_t done = (warmupFrames_ - warmupRemaining_) + capturedFrames_;
        return (std::min)(1.0f, static_cast<float>(done) / static_cast<float>(total));
    }

    void GpuTimingStatsCollector::Tick(
        const std::array<GpuTimingResult, GpuTimestampProfiler::kSlotCount>& results,
        float cpuFrameMs,
        float fpsValue)
    {
        if (state_ == State::Idle || state_ == State::Finished) {
            return;
        }

        // ウォームアップ中のフレームは捨てる。起動直後は GPU クロックが上がりきっておらず、
        // シェーダーやパイプラインのキャッシュも温まっていないため、混ぜると分布が歪む。
        if (state_ == State::Warmup) {
            if (warmupRemaining_ > 0) {
                --warmupRemaining_;
            }
            if (warmupRemaining_ == 0) {
                state_ = State::Capturing;
            }
            return;
        }

        for (uint32_t i = 0; i < GpuTimestampProfiler::kSlotCount; ++i) {
            const GpuTimingResult& r = results[i];
            SlotSamples& slot = slots_[i];

            // 名前・カテゴリはスロットが有効になった時点のものを採用する
            // （動的スロットは実行されるまで名前が空）。
            if (r.name && r.name[0] != '\0' && slot.name.empty()) {
                slot.name = r.name;
                slot.category = r.category;
            }
            // 採否はプロファイラ側の単調フラグに従う。毎フレームの値で判定すると
            // ほぼ空のパスで採否が揺れ、キャプチャ中に CSV の列構成が変わってしまう。
            if (r.everActive) {
                slot.everActive = true;
            }
            slot.gpuMs.push_back(r.gpuMs);
            slot.cpuMs.push_back(r.cpuMs);
        }

        frameGpuSamples_.push_back(results[static_cast<uint32_t>(GpuTimestampSlot::Total)].gpuMs);
        frameCpuSamples_.push_back(cpuFrameMs);
        fpsSamples_.push_back(fpsValue);

        ++capturedFrames_;
        if (capturedFrames_ >= captureFrames_) {
            Finish();
        }
    }

    void GpuTimingStatsCollector::Finish()
    {
        summaries_.clear();
        categorySummaries_.clear();

        const size_t frameCount = frameGpuSamples_.size();

        // ── パス別 ────────────────────────────────────────────
        for (uint32_t i = 0; i < GpuTimestampProfiler::kSlotCount; ++i) {
            const SlotSamples& slot = slots_[i];
            if (!slot.everActive || slot.name.empty()) {
                continue;
            }
            if (slot.category == GpuTimingCategory::Frame) {
                continue; // フレーム合計は専用の系列として別に出す
            }
            summaries_.push_back(MakeSummary(slot.name, slot.category, slot.gpuMs, &slot.cpuMs));
        }
        std::sort(summaries_.begin(), summaries_.end(),
            [](const GpuTimingSummary& a, const GpuTimingSummary& b) {
                return a.gpuMedianMs > b.gpuMedianMs;
            });

        // ── カテゴリ別（毎フレームの合計に対する統計）───────────
        // 「各パスの中央値を足す」のは誤り（別々のフレームの値を足すことになる）。
        // フレームごとに合計してから統計を取る。
        std::vector<float> waterPerFrame(frameCount, 0.0f);
        std::vector<float> auxPerFrame(frameCount, 0.0f);

        for (GpuTimingCategory category : GpuTimestampProfiler::kCategoryDisplayOrder) {
            std::vector<float> perFrameSum(frameCount, 0.0f);
            bool any = false;

            for (uint32_t i = 0; i < GpuTimestampProfiler::kSlotCount; ++i) {
                const SlotSamples& slot = slots_[i];
                if (!slot.everActive || slot.category != category) {
                    continue;
                }
                any = true;
                const size_t count = (std::min)(frameCount, slot.gpuMs.size());
                for (size_t f = 0; f < count; ++f) {
                    perFrameSum[f] += slot.gpuMs[f];
                }
            }
            if (!any) {
                continue;
            }
            categorySummaries_.push_back(MakeSummary(
                GpuTimestampProfiler::GetCategoryLabel(category), category, perFrameSum));

            if (category == GpuTimingCategory::Water) {
                waterPerFrame = perFrameSum;
            }
        }

        // 補助 View（反射ビュー等）の合計。平面反射という設計判断の代償を
        // 「シーンをもう一度描く分の ms」として単独で出せるようにする。
        for (uint32_t i = 0; i < GpuTimestampProfiler::kSlotCount; ++i) {
            const SlotSamples& slot = slots_[i];
            if (!slot.everActive || !IsAuxViewSlot(slot.name)) {
                continue;
            }
            const size_t count = (std::min)(frameCount, slot.gpuMs.size());
            for (size_t f = 0; f < count; ++f) {
                auxPerFrame[f] += slot.gpuMs[f];
            }
        }

        // 水面占有率はフレームごとに比率を出してから統計する
        std::vector<float> sharePerFrame;
        sharePerFrame.reserve(frameCount);
        for (size_t f = 0; f < frameCount; ++f) {
            const float total = frameGpuSamples_[f];
            sharePerFrame.push_back(total > 0.0001f ? (waterPerFrame[f] / total * 100.0f) : 0.0f);
        }

        frameGpu_ = MakeSummary("Frame Total (GPU)", GpuTimingCategory::Frame, frameGpuSamples_);
        frameCpu_ = MakeSummary("Frame (CPU)", GpuTimingCategory::Frame, frameCpuSamples_);
        fps_ = MakeSummary("FPS", GpuTimingCategory::Frame, fpsSamples_);
        auxView_ = MakeSummary("Aux View 合計 (GPU)", GpuTimingCategory::Frame, auxPerFrame);
        waterSharePercent_ = MakeSummary("Water 占有率 (%)", GpuTimingCategory::Water, sharePerFrame);

        state_ = State::Finished;
    }

    bool GpuTimingStatsCollector::ExportCsv(
        const std::string& directory,
        const GpuTimingCaptureMeta& meta,
        std::string& outSummaryPath) const
    {
        if (!HasResult()) {
            return false;
        }

        std::error_code ec;
        std::filesystem::path outputDir(directory);
        if (!outputDir.empty() && !std::filesystem::exists(outputDir, ec)) {
            std::filesystem::create_directories(outputDir, ec);
            if (ec) {
                Logger::GetInstance().Logf(LogLevel::Error, LogCategory::System,
                    "GpuTimingStatsCollector: 出力ディレクトリの作成に失敗しました");
                return false;
            }
        }

        const std::string stamp = MakeTimestampString();
        const std::string labelPart = meta.label.empty() ? std::string() : "_" + SanitizeForFileName(meta.label);
        const std::filesystem::path summaryPath = outputDir / ("Timing_" + stamp + labelPart + "_summary.csv");
        const std::filesystem::path framesPath = outputDir / ("Timing_" + stamp + labelPart + "_frames.csv");

        // Excel が UTF-8 と判定できるよう BOM を付ける（label / note に日本語が入るため）
        static constexpr const char* kBom = "\xEF\xBB\xBF";

        // ── 集計 CSV ──────────────────────────────────────────
        {
            std::ofstream file(summaryPath, std::ios::binary);
            if (!file) {
                Logger::GetInstance().Logf(LogLevel::Error, LogCategory::System,
                    "GpuTimingStatsCollector: 集計 CSV を開けませんでした");
                return false;
            }
            file << kBom;
            file << "# CoreEngine GPU/CPU Timing Capture\n";
            file << "# label," << CsvCell(meta.label) << "\n";
            file << "# gpu," << CsvCell(meta.gpuName) << "\n";
            file << "# build," << CsvCell(meta.buildConfig) << "\n";
            file << "# resolution," << meta.widthPixels << "x" << meta.heightPixels << "\n";
            file << "# warmup_frames," << warmupFrames_ << "\n";
            file << "# captured_frames," << capturedFrames_ << "\n";
            file << "# note," << CsvCell(meta.note) << "\n";
            file << "\n";

            file << "section,name,gpu_mean_ms,gpu_median_ms,gpu_p95_ms,gpu_min_ms,gpu_max_ms,cpu_mean_ms,cpu_median_ms,samples\n";

            auto writeRow = [&file](const char* section, const GpuTimingSummary& s) {
                file << section << "," << CsvCell(s.name) << ","
                    << s.gpuMeanMs << "," << s.gpuMedianMs << "," << s.gpuP95Ms << ","
                    << s.gpuMinMs << "," << s.gpuMaxMs << ","
                    << s.cpuMeanMs << "," << s.cpuMedianMs << "," << s.sampleCount << "\n";
            };

            writeRow("frame", frameGpu_);
            writeRow("frame", frameCpu_);
            writeRow("frame", fps_);
            writeRow("frame", auxView_);
            writeRow("frame", waterSharePercent_);

            for (const auto& s : categorySummaries_) {
                writeRow("category", s);
            }
            for (const auto& s : summaries_) {
                file << "pass," << CsvCell(std::string(GpuTimestampProfiler::GetCategoryLabel(s.category)) + " / " + s.name) << ","
                    << s.gpuMeanMs << "," << s.gpuMedianMs << "," << s.gpuP95Ms << ","
                    << s.gpuMinMs << "," << s.gpuMaxMs << ","
                    << s.cpuMeanMs << "," << s.cpuMedianMs << "," << s.sampleCount << "\n";
            }
        }

        // ── 生フレーム CSV（分布・スパイクの確認用）─────────────
        {
            std::ofstream file(framesPath, std::ios::binary);
            if (file) {
                file << kBom;
                file << "frame,frame_total_gpu_ms,frame_cpu_ms,fps";

                std::vector<uint32_t> exportSlots;
                for (uint32_t i = 0; i < GpuTimestampProfiler::kSlotCount; ++i) {
                    const SlotSamples& slot = slots_[i];
                    if (!slot.everActive || slot.name.empty()) {
                        continue;
                    }
                    exportSlots.push_back(i);
                    file << "," << CsvCell(slot.name);
                }
                file << "\n";

                for (size_t f = 0; f < frameGpuSamples_.size(); ++f) {
                    file << f << "," << frameGpuSamples_[f] << ","
                        << frameCpuSamples_[f] << "," << fpsSamples_[f];
                    for (const uint32_t slotIndex : exportSlots) {
                        const auto& values = slots_[slotIndex].gpuMs;
                        file << "," << (f < values.size() ? values[f] : 0.0f);
                    }
                    file << "\n";
                }
            }
        }

        outSummaryPath = summaryPath.string();
        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System,
            "GpuTimingStatsCollector: 計測結果を CSV へ出力しました");
        return true;
    }
}
