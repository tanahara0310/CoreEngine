#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

namespace CoreEngine
{
    /// @brief ワーカースレッドの OS 優先度
    enum class WorkerPriority
    {
        Normal,      ///< メインスレッドと同格。起動時のブロッキングな並列処理向け
        BelowNormal, ///< メインスレッドに譲る。ゲーム中のバックグラウンドロード向け
    };

    /// @brief スレッドプールの生成パラメータ
    struct ThreadPoolDesc
    {
        /// @brief プール名。OS スレッド名とプロファイラ UI の表示に使う
        std::string name = "Pool";

        /// @brief 希望ワーカー数。0 なら ThreadBudget の残り枠から自動決定
        /// @note 実際の数は ThreadBudget によって切り下げられることがある。
        ///       確定値は GetThreadCount() で取ること。
        uint32_t threadCount = 0;

        WorkerPriority priority = WorkerPriority::Normal;

        /// @brief ThreadBudget の枠から取らずに threadCount 本を作る（上限 --workers は守る）
        /// @note ほかのプールに仕事がほとんど無い間だけ動く一時的なプール（ゲームの書き出しなど）に使う
        bool borrowBudget = false;
    };

    /// @brief 汎用スレッドプール — Submit で任意のタスクをワーカーへ投入し、future で結果を受け取る
    /// @warning ワーカー内から同じプールへ Submit して素の `get()` で待つとデッドロックする。
    ///          その形が必要なら `Wait()` を使うこと。
    /// @warning タスクが触るデータの排他は見ていない（`AssetDatabase::FindAssetPath` は要注意）。
    class ThreadPool
    {
    public:
        /// @brief ワーカー数の上限
        static constexpr uint32_t kMaxWorkers = 32;

        explicit ThreadPool(const ThreadPoolDesc& desc);

        /// @brief ワーカー数だけを指定する簡易コンストラクタ
        /// @param threadCount 0 なら ThreadBudget の残り枠から自動決定
        explicit ThreadPool(uint32_t threadCount = 0);

        ~ThreadPool();

        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;
        ThreadPool(ThreadPool&&) = delete;
        ThreadPool& operator=(ThreadPool&&) = delete;

        /// @brief タスクをキューに投入し、結果の future を返す
        /// @param label プロファイラ UI に出す名前。短く、何をしているかが分かる文字列
        /// @note Shutdown 済みのプールへ投入した場合は呼び出しスレッドで同期実行し、
        ///       完了済み future を返す（終了処理中の非同期ロードで落とさないため）。
        template<typename F, typename... Args>
        auto Submit(std::string_view label, F&& func, Args&&... args)
            -> std::future<std::invoke_result_t<F, Args...>>;

        /// @brief future を待つ。待っている間、キューの未処理タスクを自分で引き受ける
        /// @details ワーカーの中から呼んでもデッドロックしないのが素の `get()` との違い。
        ///          メインスレッドから呼んだ場合も、遊ばせずに fan-out の一部を
        ///          肩代わりするので純粋に速くなる。
        template<typename FutureLike>
        void Wait(FutureLike& future);

        /// @brief キューにあるタスクを 1 件だけこのスレッドで実行する
        /// @return 実行したら true / キューが空なら false
        /// @note 任意のスレッドから呼んでよい（メインスレッドの手伝いにも使える）。
        bool TryRunOnePendingTask();

        /// @brief 現在のスレッドがこのプールのワーカーかどうか
        bool IsWorkerThread() const;

        /// @brief 全ワーカーを停止する（デストラクタから自動呼出し）
        /// @note 停止前にキューに残っているタスクは全て実行してから終わる。
        void Shutdown();

        uint32_t GetThreadCount() const { return static_cast<uint32_t>(workers_.size()); }
        const std::string& GetName() const { return name_; }

        /// @brief プールの統計スナップショット
        struct Stats
        {
            std::string name;
            uint32_t workerCount = 0;
            uint32_t activeTasks = 0;      ///< 実行中タスク数（= ビジーなワーカー数）
            size_t   pendingTasks = 0;     ///< キューで待っているタスク数
            uint64_t totalSubmitted = 0;   ///< 作ってからの総投入数
            uint64_t totalCompleted = 0;   ///< 作ってからの総完了数
            double   totalBusyMs = 0.0;    ///< 全ワーカーの実行時間の合計
        };

        /// @brief 現在の統計を取得
        Stats GetStats() const;

    private:
        /// @brief キューに積まれたタスク 1 件（本体とラベル）
        struct Task
        {
            std::function<void()> fn;
            std::string           label;
        };

        void WorkerLoop();

        /// @brief ワーカーを起動する（両コンストラクタの共通処理）
        void Launch(const ThreadPoolDesc& desc);

        /// @brief タスク 1 件を実行し、統計を記録する
        void RunTask(Task&& task);

        /// @brief キューから 1 件取り出す（取れなければ false）
        bool PopTask(Task& outTask);

        std::string    name_;
        WorkerPriority priority_ = WorkerPriority::Normal;
        uint32_t       reservedThreads_ = 0;

        std::vector<std::thread>  workers_;
        std::queue<Task>          tasks_;
        mutable std::mutex        queueMutex_;
        std::condition_variable   condition_;
        bool                      stopping_ = false;

        // 統計は atomic で持ち、GetStats が queueMutex_ を取らずに読めるようにする
        std::atomic<size_t>   pendingCount_{ 0 };
        std::atomic<uint32_t> activeTasks_{ 0 };
        std::atomic<uint64_t> totalSubmitted_{ 0 };
        std::atomic<uint64_t> totalCompleted_{ 0 };
        std::atomic<int64_t>  totalBusyNs_{ 0 };   ///< 全ワーカーの実行時間の合計（ナノ秒）
    };

    // ─────────────────────────────────────────────────────────────
    // テンプレート実装（ヘッダに置く必要がある）
    // ─────────────────────────────────────────────────────────────
    template<typename F, typename... Args>
    auto ThreadPool::Submit(std::string_view label, F&& func, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>
    {
        using ReturnType = std::invoke_result_t<F, Args...>;

        auto packaged = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(func), std::forward<Args>(args)...)
        );

        std::future<ReturnType> result = packaged->get_future();

        Task task;
        task.fn = [packaged]() { (*packaged)(); };
        task.label = std::string(label);

        bool runInline = false;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            if (stopping_) {
                runInline = true;
            } else {
                tasks_.push(std::move(task));
                pendingCount_.fetch_add(1, std::memory_order_relaxed);
                totalSubmitted_.fetch_add(1, std::memory_order_relaxed);
            }
        }

        if (runInline) {
            // 停止済みプールへの投入は同期実行へ劣化させる（詳細は Submit のコメント）
            task.fn();
            return result;
        }

        condition_.notify_one();
        return result;
    }

    template<typename FutureLike>
    void ThreadPool::Wait(FutureLike& future)
    {
        if (!future.valid()) {
            return;
        }
        while (future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            if (!TryRunOnePendingTask()) {
                // 手伝えるタスクが無いなら素直にブロックして待つ
                future.wait();
                return;
            }
        }
    }
}
