#include "ThreadPool.h"

#include <algorithm>

namespace CoreEngine
{
    ThreadPool::ThreadPool(uint32_t threadCount)
    {
        if (threadCount == 0) {
            threadCount = std::max(1u, std::thread::hardware_concurrency());
        }

        workers_.reserve(threadCount);
        for (uint32_t i = 0; i < threadCount; ++i) {
            workers_.emplace_back(&ThreadPool::WorkerLoop, this);
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

    size_t ThreadPool::GetPendingTaskCount() const
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        return tasks_.size();
    }

    void ThreadPool::WorkerLoop()
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

            task();
        }
    }
}
