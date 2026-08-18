#include "pch.h"
#include "CpuProfiler.h"

#include "Utility/Logger/Logger.h"

#include <Windows.h>

#include <algorithm>
#include <chrono>

namespace CoreEngine
{
    namespace
    {
        /// @brief 実行中スコープ（スレッドごとに積む）
        struct ActiveScope {
            const char* name = nullptr;
            uint64_t    sequence = 0;
            uint32_t    depth = 0;
            std::chrono::steady_clock::time_point wallBegin{};
            double      cpuBeginMs = 0.0;
        };

        // スコープのスタックはスレッドごとに独立。こうしないと
        // ワーカーの区間がメインスレッドの子として記録されて系統が壊れる
        thread_local std::vector<ActiveScope> tlsScopeStack;
    }

    double GetCurrentThreadCpuMilliseconds()
    {
        FILETIME creationTime{};
        FILETIME exitTime{};
        FILETIME kernelTime{};
        FILETIME userTime{};

        if (!GetThreadTimes(GetCurrentThread(), &creationTime, &exitTime, &kernelTime, &userTime)) {
            return 0.0;
        }

        // FILETIME は 100ns 単位。カーネル時間も足すのは、ファイル I/O や
        // ロック待ちの一部がカーネル側に計上されるため
        const auto toTicks = [](const FILETIME& fileTime) -> uint64_t {
            return (static_cast<uint64_t>(fileTime.dwHighDateTime) << 32) | fileTime.dwLowDateTime;
        };

        const uint64_t ticks = toTicks(kernelTime) + toTicks(userTime);
        return static_cast<double>(ticks) / 10000.0;   // 100ns -> ms
    }

    CpuProfiler& CpuProfiler::GetInstance()
    {
        static CpuProfiler instance;
        return instance;
    }

    void CpuProfiler::BeginScope(const char* name)
    {
        // enabled_ はここでは見ない: 無効化はレポートへの「収集」を止めるだけで、
        // 計測（EndScope の戻り値）は常に生きている必要がある（ヘッダの SetEnabled 参照）
        if (!name) {
            return;
        }

        ActiveScope scope;
        scope.name = name;
        scope.depth = static_cast<uint32_t>(tlsScopeStack.size());
        scope.wallBegin = std::chrono::steady_clock::now();
        scope.cpuBeginMs = GetCurrentThreadCpuMilliseconds();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            scope.sequence = nextSequence_++;
        }

        tlsScopeStack.push_back(scope);
    }

    CpuProfiler::Sample CpuProfiler::EndScope()
    {
        if (tlsScopeStack.empty()) {
            return {};
        }

        const ActiveScope scope = tlsScopeStack.back();
        tlsScopeStack.pop_back();

        Sample sample;
        sample.name = scope.name ? scope.name : "";
        sample.sequence = scope.sequence;
        sample.depth = scope.depth;
        sample.threadId = GetCurrentThreadId();
        sample.wallMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - scope.wallBegin).count();
        sample.cpuMs = GetCurrentThreadCpuMilliseconds() - scope.cpuBeginMs;

        // enabled_ が絡むのはこの「レポート用の蓄積」だけ。計測値は常に返す
        if (enabled_) {
            std::lock_guard<std::mutex> lock(mutex_);
            samples_.push_back(sample);
        }
        return sample;
    }

    void CpuProfiler::Reset()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        samples_.clear();
        nextSequence_ = 0;
    }

    std::vector<CpuProfiler::Sample> CpuProfiler::GetSamples() const
    {
        std::vector<Sample> sorted;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            sorted = samples_;
        }

        // サンプルはスコープ「終了」順に積まれるので、親が子より後ろに来る。
        // 開始順に並べ替えるとネスト構造がそのままツリーになる
        std::sort(sorted.begin(), sorted.end(),
            [](const Sample& a, const Sample& b) { return a.sequence < b.sequence; });
        return sorted;
    }

    void CpuProfiler::LogReport(const char* title) const
    {
        const std::vector<Sample> samples = GetSamples();
        if (samples.empty()) {
            return;
        }

        Logger& logger = Logger::GetInstance();
        const uint32_t mainThreadId = GetCurrentThreadId();

        logger.Logf(LogLevel::Info, LogCategory::System,
            "[CpuProfile] {} — 壁時計 / CPU / 待ち率（★待ち率が高い区間は「そのステップが重い」ではなく"
            "「他スレッドと競合して待っている」。CPU 時間の分解能は約 15.6ms なので"
            "数十 ms 未満の行の待ち率は読まないこと）", title);

        for (const Sample& sample : samples) {
            // 待ち率は「その区間で自スレッドが走っていなかった割合」。
            // 分解能より短い区間は判定不能として '-' を出す
            const bool measurable = sample.wallMs >= 50.0;
            const double waitRatio = (sample.wallMs > 0.0)
                ? (1.0 - (sample.cpuMs / sample.wallMs)) * 100.0
                : 0.0;

            const std::string indent(static_cast<size_t>(sample.depth) * 2, ' ');
            const std::string threadTag =
                (sample.threadId == mainThreadId) ? "main  " : "worker";

            if (measurable) {
                logger.Logf(LogLevel::Info, LogCategory::System,
                    "[CpuProfile] {:8.3f}s wall | {:8.3f}s cpu | 待ち {:5.1f}% | {} | {}{}",
                    sample.wallMs / 1000.0, sample.cpuMs / 1000.0,
                    (std::max)(0.0, waitRatio), threadTag, indent, sample.name);
            } else {
                logger.Logf(LogLevel::Info, LogCategory::System,
                    "[CpuProfile] {:8.3f}s wall | {:8.3f}s cpu | 待ち     - | {} | {}{}",
                    sample.wallMs / 1000.0, sample.cpuMs / 1000.0,
                    threadTag, indent, sample.name);
            }
        }
    }
}
