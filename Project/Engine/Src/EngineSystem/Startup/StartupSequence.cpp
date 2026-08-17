#include "pch.h"
#include "StartupSequence.h"

#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <chrono>

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
        tasks_.push_back(Entry{ std::move(task), {}, 0.0 });
    }

    void StartupSequence::Step()
    {
        if (!HasNext()) {
            return;
        }

        Entry& entry = tasks_[cursor_];
        entry.executedLabel = entry.task->GetLabel();

        const auto begin = std::chrono::steady_clock::now();
        entry.task->Execute();
        const auto end = std::chrono::steady_clock::now();

        entry.seconds = std::chrono::duration<double>(end - begin).count();
        totalSeconds_ += entry.seconds;
        ++cursor_;

        // 最初のステップでログシステムを初期化するため、ログは Execute の後に出す
        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System,
            "[Startup] {:2}/{:2}  {:6.3f}s  {}",
            cursor_, tasks_.size(), entry.seconds, entry.executedLabel);
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
        return static_cast<float>(cursor_) / static_cast<float>(tasks_.size());
    }

    void StartupSequence::LogSummary() const
    {
        auto& logger = Logger::GetInstance();

        logger.Logf(LogLevel::Info, LogCategory::System,
            "[Startup] 完了: {} ステップ / 合計 {:.3f}s", cursor_, totalSeconds_);

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
            logger.Logf(LogLevel::Info, LogCategory::System,
                "[Startup]   {:6.3f}s ({:4.1f}%)  {}",
                sorted[i]->seconds, ratio, sorted[i]->executedLabel);
        }
    }
}
