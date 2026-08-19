#include "pch.h"
#include "ThreadPool.h"

#include "ThreadBudget.h"

#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif

namespace
{
    /// @brief 現在のスレッドがどのプールのワーカーか（IsWorkerThread の実体）
    /// @details スレッド ID の配列を線形探索するより速く、ロックも要らない。
    thread_local const CoreEngine::ThreadPool* t_currentPool = nullptr;

    /// @brief ワーカーインデックス（統計記録用）
    thread_local uint32_t t_currentWorkerIndex = 0;

#ifdef _WIN32
    /// @brief デバッガに出るスレッド名（"<プール名> Worker N"）を設定する
    void ApplyThreadName(std::thread& thread, const std::string& poolName, uint32_t index)
    {
        // OS スレッド名を付けないと VS のパフォーマンスプロファイラや WPA で
        // 「Thread 12345」としか出ず、どのプールの負荷なのか区別が付かない。
        // 自作プロファイラは回帰検知、標準ツールは原因究明という役割分担なので、
        // 標準ツール側で読めないのは実質的な欠落である。
        const std::wstring name = L"CoreEngine " +
            std::wstring(poolName.begin(), poolName.end()) +
            L" Worker " + std::to_wstring(index);
        SetThreadDescription(static_cast<HANDLE>(thread.native_handle()), name.c_str());
    }

    /// @brief ワーカースレッドの優先度を OS へ設定する
    void ApplyThreadPriority(std::thread& thread, CoreEngine::WorkerPriority priority)
    {
        const int osPriority = (priority == CoreEngine::WorkerPriority::BelowNormal)
            ? THREAD_PRIORITY_BELOW_NORMAL
            : THREAD_PRIORITY_NORMAL;
        SetThreadPriority(static_cast<HANDLE>(thread.native_handle()), osPriority);
    }
#else
    void ApplyThreadName(std::thread&, const std::string&, uint32_t) {}
    void ApplyThreadPriority(std::thread&, CoreEngine::WorkerPriority) {}
#endif
}

namespace CoreEngine
{
    ThreadPool::ThreadPool(const ThreadPoolDesc& desc)
    {
        Launch(desc);
    }

    ThreadPool::ThreadPool(uint32_t threadCount)
    {
        ThreadPoolDesc desc;
        desc.threadCount = threadCount;
        Launch(desc);
    }

    void ThreadPool::Launch(const ThreadPoolDesc& desc)
    {
        name_ = desc.name;
        priority_ = desc.priority;

        // 希望数をそのまま使わず、プロセス全体の枠から割り当ててもらう。
        // プールが 3 つ独立に hardware_concurrency/2 を取るとコア数を超える。
        uint32_t granted = ThreadBudget::Reserve(desc.threadCount);
        granted = (std::min)(granted, kMaxWorkers);
        reservedThreads_ = granted;

        epoch_ = std::chrono::steady_clock::now();
        recentTasks_.reserve(kRecentTaskCapacity);

        workers_.reserve(granted);
        for (uint32_t i = 0; i < granted; ++i) {
            workers_.emplace_back(&ThreadPool::WorkerLoop, this, i);
            ApplyThreadName(workers_.back(), name_, i);
            ApplyThreadPriority(workers_.back(), priority_);
        }
    }

    ThreadPool::~ThreadPool()
    {
        Shutdown();
        ThreadBudget::Release(reservedThreads_);
    }

    void ThreadPool::Shutdown()
    {
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (stopping_) {
                return;
            }
            stopping_ = true;
        }

        condition_.notify_all();

        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    bool ThreadPool::IsWorkerThread() const
    {
        return t_currentPool == this;
    }

    bool ThreadPool::PopTask(Task& outTask)
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (tasks_.empty()) {
            return false;
        }
        outTask = std::move(tasks_.front());
        tasks_.pop();
        pendingCount_.fetch_sub(1, std::memory_order_relaxed);
        return true;
    }

    bool ThreadPool::TryRunOnePendingTask()
    {
        Task task;
        if (!PopTask(task)) {
            return false;
        }

        // このプールのワーカー以外（メインスレッドや別プールのワーカー）が
        // 手伝いに来た場合は kMaxWorkers を渡してワーカー枠の統計に触らせない。
        // 0 番として記録すると、本物の 0 番ワーカーと同じスロットを 2 スレッドが
        // 書き合って「実行中タスク名」が入れ替わる。
        const uint32_t index = (t_currentPool == this) ? t_currentWorkerIndex : kMaxWorkers;
        RunTask(std::move(task), index);
        return true;
    }

    void ThreadPool::RunTask(Task&& task, uint32_t workerIndex)
    {
        const auto startedAt = std::chrono::steady_clock::now();
        const double waitMs =
            std::chrono::duration<double, std::milli>(startedAt - task.enqueuedAt).count();

        const uint32_t active = activeTasks_.fetch_add(1, std::memory_order_relaxed) + 1;

        if (workerIndex < kMaxWorkers) {
            std::lock_guard<std::mutex> lock(statsMutex_);
            WorkerState& slot = workerStates_[workerIndex];
            slot.busy = true;
            slot.currentTask = task.label;
            workerTaskStart_[workerIndex] = startedAt;
            peakActiveTasks_ = (std::max)(peakActiveTasks_, active);
        }

        // タスク本体はロックの外で実行する。
        // packaged_task が例外を future へ載せるのでここまで飛んでは来ないが、
        // 統計の整合性を例外に依存させないため後片付けは無条件に行う。
        task.fn();

        const auto finishedAt = std::chrono::steady_clock::now();
        const double runMs =
            std::chrono::duration<double, std::milli>(finishedAt - startedAt).count();

        {
            std::lock_guard<std::mutex> lock(statsMutex_);

            // epoch_ は ResetStats がこのロックの内側で書き換えるので、
            // epoch_ を使う計算もロックの内側で行う。外に出すとデータ競合になる
            const double startMs =
                std::chrono::duration<double, std::milli>(startedAt - epoch_).count();

            if (workerIndex < kMaxWorkers) {
                WorkerState& slot = workerStates_[workerIndex];
                slot.busy = false;
                slot.currentTask.clear();
                slot.currentElapsedMs = 0.0;
                slot.completedTasks += 1;
                slot.busyMs += runMs;
            }

            totalBusyMs_ += runMs;
            totalWaitMs_ += waitMs;
            maxWaitMs_ = (std::max)(maxWaitMs_, waitMs);
            maxRunMs_ = (std::max)(maxRunMs_, runMs);

            // 稼働区間を伸ばす（占有率・直列比の分母）
            const double endMs =
                std::chrono::duration<double, std::milli>(finishedAt - epoch_).count();
            if (!hasActivity_) {
                hasActivity_ = true;
                firstTaskStartMs_ = startMs;
            }
            firstTaskStartMs_ = (std::min)(firstTaskStartMs_, startMs);
            lastTaskEndMs_ = (std::max)(lastTaskEndMs_, endMs);

            TaskRecord record;
            record.label = std::move(task.label);
            record.workerIndex = workerIndex;
            record.waitMs = waitMs;
            record.runMs = runMs;
            record.startMs = startMs;

            if (recentTasks_.size() < kRecentTaskCapacity) {
                recentTasks_.push_back(std::move(record));
            } else {
                recentTasks_[recentHead_] = std::move(record);
                recentHead_ = (recentHead_ + 1) % kRecentTaskCapacity;
            }
        }

        activeTasks_.fetch_sub(1, std::memory_order_relaxed);
        totalCompleted_.fetch_add(1, std::memory_order_relaxed);
    }

    void ThreadPool::WorkerLoop(uint32_t workerIndex)
    {
        t_currentPool = this;
        t_currentWorkerIndex = workerIndex;

        while (true) {
            Task task;

            {
                std::unique_lock<std::mutex> lock(queueMutex_);
                condition_.wait(lock, [this]() {
                    return stopping_ || !tasks_.empty();
                });

                if (stopping_ && tasks_.empty()) {
                    break;
                }

                task = std::move(tasks_.front());
                tasks_.pop();
                pendingCount_.fetch_sub(1, std::memory_order_relaxed);
            }

            RunTask(std::move(task), workerIndex);
        }

        t_currentPool = nullptr;
    }

    ThreadPool::Stats ThreadPool::GetStats() const
    {
        Stats stats;
        stats.name = name_;
        stats.workerCount = static_cast<uint32_t>(workers_.size());
        stats.activeTasks = activeTasks_.load(std::memory_order_relaxed);
        stats.pendingTasks = pendingCount_.load(std::memory_order_relaxed);
        stats.totalSubmitted = totalSubmitted_.load(std::memory_order_relaxed);
        stats.totalCompleted = totalCompleted_.load(std::memory_order_relaxed);

        const auto now = std::chrono::steady_clock::now();

        std::lock_guard<std::mutex> lock(statsMutex_);
        stats.elapsedMs = std::chrono::duration<double, std::milli>(now - epoch_).count();
        stats.activeSpanMs = hasActivity_
            ? (std::max)(0.0, lastTaskEndMs_ - firstTaskStartMs_) : 0.0;
        stats.totalBusyMs = totalBusyMs_;
        stats.totalWaitMs = totalWaitMs_;
        stats.maxWaitMs = maxWaitMs_;
        stats.maxRunMs = maxRunMs_;
        stats.peakActiveTasks = peakActiveTasks_;

        for (uint32_t i = 0; i < stats.workerCount && i < kMaxWorkers; ++i) {
            stats.workers[i] = workerStates_[i];
            // 実行中タスクの経過時間はスナップショット時点で計算する。
            // タスク実行中はワーカー自身がロックを取れないので、
            // 記録側で更新する方式では常に古い値しか見えない。
            if (stats.workers[i].busy) {
                stats.workers[i].currentElapsedMs =
                    std::chrono::duration<double, std::milli>(now - workerTaskStart_[i]).count();
            }
        }

        return stats;
    }

    std::vector<ThreadPool::TaskRecord> ThreadPool::GetRecentTasks() const
    {
        std::lock_guard<std::mutex> lock(statsMutex_);

        std::vector<TaskRecord> result;
        result.reserve(recentTasks_.size());

        if (recentTasks_.size() < kRecentTaskCapacity) {
            result = recentTasks_;
            return result;
        }

        // リングバッファを古い順に並べ直す
        for (size_t i = 0; i < kRecentTaskCapacity; ++i) {
            result.push_back(recentTasks_[(recentHead_ + i) % kRecentTaskCapacity]);
        }
        return result;
    }

    void ThreadPool::ResetStats()
    {
        totalSubmitted_.store(0, std::memory_order_relaxed);
        totalCompleted_.store(0, std::memory_order_relaxed);

        std::lock_guard<std::mutex> lock(statsMutex_);
        epoch_ = std::chrono::steady_clock::now();
        recentTasks_.clear();
        recentHead_ = 0;
        totalBusyMs_ = 0.0;
        totalWaitMs_ = 0.0;
        maxWaitMs_ = 0.0;
        maxRunMs_ = 0.0;
        peakActiveTasks_ = 0;
        hasActivity_ = false;
        firstTaskStartMs_ = 0.0;
        lastTaskEndMs_ = 0.0;

        for (auto& slot : workerStates_) {
            slot.completedTasks = 0;
            slot.busyMs = 0.0;
        }
    }
}
