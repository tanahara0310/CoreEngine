#include "pch.h"
#include "ThreadPool.h"

#include "ThreadBudget.h"
#include "Utility/Logger/Logger.h"
#include "Utility/Profiler/ProfileZone.h"

#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif

namespace
{
    /// @brief 現在のスレッドがどのプールのワーカーか（IsWorkerThread の実体）
    /// @details スレッド ID の配列を線形探索するより速く、ロックも要らない。
    thread_local const CoreEngine::ThreadPool* t_currentPool = nullptr;

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
        // 枠を借りるプールは枠から取らず、上限だけを守る。
        const uint32_t limit = ThreadBudget::GetWorkerLimit();
        uint32_t granted = 0;
        if (desc.borrowBudget) {
            granted = (std::max)(1u, desc.threadCount);
            granted = (limit > 0) ? (std::min)(granted, limit) : granted;
            reservedThreads_ = 0;
        } else {
            granted = ThreadBudget::Reserve(desc.threadCount);
            reservedThreads_ = granted;
        }
        granted = (std::min)(granted, kMaxWorkers);

        Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::System, "[Threads] {}: ワーカー {} 本（枠 {}・上限 {}{}）",
            name_, granted, ThreadBudget::GetCapacity(), limit > 0 ? std::to_string(limit) : std::string("なし"),
            desc.borrowBudget ? "・枠を借りる" : "");

        workers_.reserve(granted);
        for (uint32_t i = 0; i < granted; ++i) {
            workers_.emplace_back(&ThreadPool::WorkerLoop, this);
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
        RunTask(std::move(task));
        return true;
    }

    void ThreadPool::RunTask(Task&& task)
    {
        const auto startedAt = std::chrono::steady_clock::now();
        activeTasks_.fetch_add(1, std::memory_order_relaxed);

        // packaged_task が例外を future へ載せるので、ここまで例外は飛んでこない
        {
            CORE_PROFILE_ZONE_TRANSIENT(task.label.c_str());
            task.fn();
        }

        const auto finishedAt = std::chrono::steady_clock::now();
        totalBusyNs_.fetch_add(
            std::chrono::duration_cast<std::chrono::nanoseconds>(finishedAt - startedAt).count(),
            std::memory_order_relaxed);
        activeTasks_.fetch_sub(1, std::memory_order_relaxed);
        totalCompleted_.fetch_add(1, std::memory_order_relaxed);
    }

    void ThreadPool::WorkerLoop()
    {
        t_currentPool = this;

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

            RunTask(std::move(task));
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
        stats.totalBusyMs = static_cast<double>(totalBusyNs_.load(std::memory_order_relaxed)) / 1.0e6;
        return stats;
    }
}
