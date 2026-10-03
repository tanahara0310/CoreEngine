#include "pch.h"
#include "StartupSequence.h"

#include "Utility/Logger/Logger.h"
#include "Utility/Profiler/CpuProfiler.h"

#include <algorithm>

namespace CoreEngine
{
    void StartupSequence::Add(std::string label, std::function<void()> action)
    {
        Add(std::make_unique<FunctionStartupTask>(std::move(label), std::move(action)));
    }

    void StartupSequence::Add(std::function<std::string()> labelProvider, std::function<void()> action)
    {
        Add(std::make_unique<FunctionStartupTask>(std::move(labelProvider), std::move(action)));
    }

    void StartupSequence::Add(std::unique_ptr<IStartupTask> task)
    {
        if (!task) {
            return;
        }
        // 実行開始後の追加は実行中エントリの参照を壊す（クラス説明の warning 参照）
        assert(cursor_ == 0 && "StartupSequence: 実行開始後にステップを追加してはいけない");
        tasks_.push_back(Entry{ std::move(task), {}, 0.0, 0.0 });
    }

    StartupTaskResult StartupSequence::Step()
    {
        if (!HasNext()) {
            return StartupTaskResult::Done;
        }

        Entry& entry = tasks_[cursor_];
        if (entry.calls == 0) {
            entry.executedLabel = entry.task->GetLabel();
            entry.firstCall = std::chrono::steady_clock::now();
        }
        ++entry.calls;

        // 計測は CpuProfiler の一本だけ。自前のストップウォッチを持つと [Startup] 行と
        // [CpuProfile] レポートが別経路の数字になり、片方だけ直したときに食い違う。
        // 例外時に EndScope が漏れるが、起動タスクの例外＝起動失敗なので追わない。
        auto& profiler = CpuProfiler::GetInstance();
        profiler.BeginScope(entry.executedLabel.c_str());
        const StartupTaskResult result = entry.task->Execute();
        const CpuProfiler::Sample measured = profiler.EndScope();

        entry.cpuSeconds += measured.cpuMs / 1000.0;
        if (result == StartupTaskResult::Pending) {
            return result;
        }

        // 待ったステップは、最初に呼んでから済むまでを壁時計にする
        entry.seconds = (entry.calls == 1)
            ? measured.wallMs / 1000.0
            : std::chrono::duration<double>(std::chrono::steady_clock::now() - entry.firstCall).count();
        totalSeconds_ += entry.seconds;
        totalCpuSeconds_ += entry.cpuSeconds;
        ++cursor_;

        // ★壁時計だけでなく CPU 時間も必ず出す。★
        // 壁時計しか無かった頃、非同期プリロードのワーカーと競合して待っている区間を
        // 「そのステップが重い」と誤読し、無意味なリファクタリングに着手しかけた。
        // 最初のステップでログシステムを初期化するため、ログは Execute の後に出す
        if (entry.calls == 1) {
            Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System,
                "[Startup] {:2}/{:2}  {:6.3f}s wall  {:6.3f}s cpu  {}",
                cursor_, tasks_.size(), entry.seconds, entry.cpuSeconds, entry.executedLabel);
        } else {
            Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System,
                "[Startup] {:2}/{:2}  {:6.3f}s wall  {:6.3f}s cpu  {}（{} 回に分けて済んだ）",
                cursor_, tasks_.size(), entry.seconds, entry.cpuSeconds, entry.executedLabel, entry.calls);
        }
        return result;
    }

    std::string StartupSequence::GetNextLabel() const
    {
        if (!HasNext()) {
            return {};
        }
        return tasks_[cursor_].task->GetLabel();
    }

    float StartupSequence::GetProgress() const
    {
        if (tasks_.empty()) {
            return 1.0f;
        }
        // 待っているステップの進み具合は、そのステップ 1 つ分の幅へ写す
        const float pending = HasNext() ? std::clamp(tasks_[cursor_].task->GetProgress(), 0.0f, 1.0f) : 0.0f;
        return std::clamp((static_cast<float>(cursor_) + pending) / static_cast<float>(tasks_.size()), 0.0f, 1.0f);
    }

    void StartupSequence::LogSummary() const
    {
        auto& logger = Logger::GetInstance();

        const double waitSeconds = (std::max)(0.0, totalSeconds_ - totalCpuSeconds_);
        logger.Logf(LogLevel::Info, LogCategory::System,
            "[Startup] 完了: {} ステップ / 壁時計 {:.3f}s（うちメインスレッドの CPU {:.3f}s / 待ち {:.3f}s）",
            cursor_, totalSeconds_, totalCpuSeconds_, waitSeconds);

        std::vector<const Entry*> sorted;
        sorted.reserve(tasks_.size());
        for (const Entry& entry : tasks_) {
            sorted.push_back(&entry);
        }
        std::sort(sorted.begin(), sorted.end(),
            [](const Entry* a, const Entry* b) { return a->seconds > b->seconds; });

        const size_t topCount = std::min<size_t>(sorted.size(), 10);
        logger.Logf(LogLevel::Info, LogCategory::System,
            "[Startup] 遅い順 上位{}件:", topCount);

        for (size_t i = 0; i < topCount; ++i) {
            const double ratio = (totalSeconds_ > 0.0) ? (sorted[i]->seconds / totalSeconds_ * 100.0) : 0.0;

            // ★壁時計と CPU の差が大きい行は「そのステップが重い」ではなく
            //   「他スレッドと競合して待っている」。潰す対象を間違えないための目印。
            //   CPU 時間の分解能は約 15.6ms なので、短い行の待ち率は読まない
            const double waitRatio = (sorted[i]->seconds > 0.0)
                ? (1.0 - sorted[i]->cpuSeconds / sorted[i]->seconds) * 100.0
                : 0.0;
            const bool measurable = sorted[i]->seconds >= 0.05;

            if (measurable) {
                logger.Logf(LogLevel::Info, LogCategory::System,
                    "[Startup]   {:6.3f}s wall ({:4.1f}%) / {:6.3f}s cpu / 待ち {:5.1f}%  {}",
                    sorted[i]->seconds, ratio, sorted[i]->cpuSeconds,
                    (std::max)(0.0, waitRatio), sorted[i]->executedLabel);
            } else {
                logger.Logf(LogLevel::Info, LogCategory::System,
                    "[Startup]   {:6.3f}s wall ({:4.1f}%) / {:6.3f}s cpu / 待ち     -  {}",
                    sorted[i]->seconds, ratio, sorted[i]->cpuSeconds, sorted[i]->executedLabel);
            }
        }
    }
}
