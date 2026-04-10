#pragma once

#include <cstdint>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <vector>
#include <condition_variable>

namespace CoreEngine
{
    /// @brief 汎用スレッドプール — Submit で任意のタスクをワーカースレッドへ投入し、
    ///        std::future 経由で結果を受け取る。
    class ThreadPool
    {
    public:
        /// @brief コンストラクタ
        /// @param threadCount ワーカースレッド数（0 の場合は hardware_concurrency を使用）
        explicit ThreadPool(uint32_t threadCount = 0);

        ~ThreadPool();

        // コピー・ムーブ禁止
        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;
        ThreadPool(ThreadPool&&) = delete;
        ThreadPool& operator=(ThreadPool&&) = delete;

        /// @brief タスクをキューに投入し、結果の future を返す
        /// @tparam F 呼び出し可能オブジェクト
        /// @tparam Args 引数型
        /// @param func 実行する関数
        /// @param args 関数の引数
        /// @return タスクの結果を受け取る future
        template<typename F, typename... Args>
        auto Submit(F&& func, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>>;

        /// @brief 全ワーカースレッドを停止する（デストラクタから自動呼出し）
        void Shutdown();

        /// @brief ワーカースレッド数を取得
        /// @return スレッド数
        uint32_t GetThreadCount() const { return static_cast<uint32_t>(workers_.size()); }

        /// @brief キュー内の未処理タスク数を取得
        /// @return 未処理タスク数
        size_t GetPendingTaskCount() const;

    private:
        void WorkerLoop();

        std::vector<std::thread> workers_;
        std::queue<std::function<void()>> tasks_;
        mutable std::mutex queueMutex_;
        std::condition_variable condition_;
        bool stopping_ = false;
    };

    // ─────────────────────────────────────────────────────────────
    // テンプレート実装（ヘッダに置く必要がある）
    // ─────────────────────────────────────────────────────────────
    template<typename F, typename... Args>
    auto ThreadPool::Submit(F&& func, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>
    {
        using ReturnType = std::invoke_result_t<F, Args...>;

        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(func), std::forward<Args>(args)...)
        );

        std::future<ReturnType> result = task->get_future();

        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (stopping_) {
                throw std::runtime_error("ThreadPool: cannot submit task after shutdown");
            }
            tasks_.emplace([task]() { (*task)(); });
        }

        condition_.notify_one();
        return result;
    }
}
