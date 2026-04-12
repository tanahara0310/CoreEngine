#include "ThreadPool.h"

#include <algorithm>

namespace CoreEngine
{
    ThreadPool::ThreadPool(uint32_t threadCount)
    {
        if (threadCount == 0) {
            threadCount = std::max(1u, std::thread::hardware_concurrency());
        }
        threadCount = std::min(threadCount, kMaxWorkers);

        // ワーカー状態を明示的に初期化
        for (uint32_t i = 0; i < kMaxWorkers; ++i) {
            workerBusy_[i].store(false, std::memory_order_relaxed);
        }

        workers_.reserve(threadCount);
        for (uint32_t i = 0; i < threadCount; ++i) {
            workers_.emplace_back(&ThreadPool::WorkerLoop, this, i);
        }
    }

    ThreadPool::~ThreadPool()
    {
        Shutdown();
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

    void ThreadPool::WorkerLoop(uint32_t workerIndex)
    {
        while (true) {
            std::function<void()> task;

            {
                std::unique_lock<std::mutex> lock(queueMutex_);
                condition_.wait(lock, [this]() {
                    return stopping_ || !tasks_.empty();
                });

                if (stopping_ && tasks_.empty()) {
                    return;
                }

                task = std::move(tasks_.front());
                tasks_.pop();
            }

            workerBusy_[workerIndex].store(true, std::memory_order_relaxed);
            activeTasks_.fetch_add(1, std::memory_order_relaxed);

            task();

            activeTasks_.fetch_sub(1, std::memory_order_relaxed);
            totalCompleted_.fetch_add(1, std::memory_order_relaxed);
            workerBusy_[workerIndex].store(false, std::memory_order_relaxed);
        }
    }

    ThreadPool::Stats ThreadPool::GetStats() const
    {
        Stats stats;
        stats.workerCount = static_cast<uint32_t>(workers_.size());
        stats.activeTasks = activeTasks_.load(std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            stats.pendingTasks = tasks_.size();
        }
        stats.totalSubmitted = totalSubmitted_.load(std::memory_order_relaxed);
        stats.totalCompleted = totalCompleted_.load(std::memory_order_relaxed);
        stats.workerBusy.resize(stats.workerCount);
        for (uint32_t i = 0; i < stats.workerCount && i < kMaxWorkers; ++i) {
            stats.workerBusy[i] = workerBusy_[i].load(std::memory_order_relaxed);
        }
        return stats;
    }
}
