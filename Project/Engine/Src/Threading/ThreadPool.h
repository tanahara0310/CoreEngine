#pragma once

#include <array>
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
    };

    /// @brief 汎用スレッドプール — Submit で任意のタスクをワーカーへ投入し、future で結果を受け取る
    ///
    /// @details **タスクに必ずラベルを付ける設計にしてある。** ラベル無しだと
    ///          プロファイラ UI が「W3 が緑」までしか言えず、
    ///          「何が」「どれだけ」走ったのかが分からない。並列化の効果測定は
    ///          タスク単位の実行時間と待ち時間が取れて初めて可能になる。
    ///
    /// @warning **ネストした待ちに注意。** ワーカーの中から同じプールへ Submit して
    ///          その future を素の `get()` で待つと、全ワーカーが待ちに入った瞬間に
    ///          デッドロックする（キューにあるタスクを実行する者が居なくなる）。
    ///          この形が避けられない場合は `Wait()` を使うこと。待っている間に
    ///          キューのタスクを自分で引き受けるので詰まらない。
    ///
    /// @warning **このクラスはタスクが触るデータの排他を一切見ていない。**
    ///          特に `AssetDatabase::FindAssetPath` は `unordered_map::operator[]` で
    ///          挿入するため、読み取りに見えて書き込みであり、ワーカーから
    ///          呼ぶとレースする。ワーカーに渡す前にメインスレッドで解決しておくこと。
    class ThreadPool
    {
    public:
        /// @brief ワーカー数の上限（統計用の固定長配列サイズ）
        static constexpr uint32_t kMaxWorkers = 32;

        /// @brief 直近のタスク実行履歴を保持する件数
        static constexpr size_t kRecentTaskCapacity = 256;

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
        /// @param func  実行する関数
        /// @param args  関数の引数
        /// @return タスクの結果を受け取る future
        /// @note Shutdown 済みのプールへ投入した場合は**呼び出しスレッドで同期実行**して
        ///       完了済み future を返す。以前は例外を投げていたが、終了処理中に
        ///       非同期ロードが飛んでくる経路（シーン破棄など）で捕まえ手が居らず
        ///       プロセスが落ちるため、同期実行への劣化に変更した。
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

        /// @brief ワーカー 1 本ぶんの状態
        struct WorkerState
        {
            bool        busy = false;             ///< 現在タスクを実行中か
            std::string currentTask;              ///< 実行中タスクのラベル（待機中は空）
            double      currentElapsedMs = 0.0;   ///< 実行中タスクの経過時間
            uint64_t    completedTasks = 0;       ///< このワーカーが完了させた総数
            double      busyMs = 0.0;             ///< このワーカーの累計実行時間
        };

        /// @brief 完了したタスク 1 件の記録
        struct TaskRecord
        {
            std::string label;

            /// @brief 実行したワーカー番号。kMaxWorkers なら
            ///        「プール外のスレッドが Wait() 経由で手伝ったぶん」
            uint32_t    workerIndex = 0;
            double      waitMs = 0.0;   ///< 投入されてから実行開始までの待ち時間
            double      runMs = 0.0;    ///< 実行時間
            double      startMs = 0.0;  ///< 計測開始（リセット時点）からの経過
        };

        /// @brief プールの統計スナップショット
        struct Stats
        {
            std::string name;
            uint32_t workerCount = 0;
            uint32_t activeTasks = 0;      ///< 実行中タスク数（= ビジーなワーカー数）
            size_t   pendingTasks = 0;     ///< キューで待っているタスク数
            uint64_t totalSubmitted = 0;   ///< 計測開始以降の総投入数
            uint64_t totalCompleted = 0;   ///< 計測開始以降の総完了数
            double   elapsedMs = 0.0;      ///< 計測開始からの壁時計
            double   activeSpanMs = 0.0;   ///< 最初のタスク開始から最後のタスク終了までの幅
            double   totalBusyMs = 0.0;    ///< 全ワーカーの実行時間の合計
            double   totalWaitMs = 0.0;    ///< 全タスクの待ち時間の合計
            double   maxWaitMs = 0.0;      ///< 最も長かった待ち時間
            double   maxRunMs = 0.0;       ///< 最も長かった実行時間
            uint32_t peakActiveTasks = 0;  ///< 同時実行数のピーク

            std::array<WorkerState, kMaxWorkers> workers{};

            /// @brief 占有率 = 全ワーカー実行時間 / (稼働区間 × ワーカー数)
            /// @details 1.0 に近ければワーカーを遊ばせていない。低い場合は
            ///          タスクの粒度が細かすぎるか、そもそも投入量が足りていない。
            ///
            /// @note **分母が「計測開始からの経過」ではなく「稼働区間」なのが要点。**
            ///       ロード系のプールは起動時に一気に働いて以降ずっと暇なので、
            ///       経過時間で割ると必ず 0% 近くになり、
            ///       「並列化が効いていない」という誤読を生む。
            ///       実際に仕事があった区間で割らないとこの数字は意味を持たない。
            double GetOccupancy() const
            {
                const double denominator = activeSpanMs * static_cast<double>(workerCount);
                return denominator > 0.0 ? totalBusyMs / denominator : 0.0;
            }

            /// @brief 直列実行との比 = 全ワーカー実行時間 / 稼働区間
            /// @details 「何本ぶんの仕事を並列でこなしたか」。8 ワーカーで 6.0 なら
            ///          直列比 6 倍。**並列化の効果はこの数字で語ること。**
            ///
            /// @warning フルロード時は 1 スレッドあたりのスループットが落ちる
            ///          （物理コアの共有・クロック低下）ので、この値は実際の
            ///          短縮率より大きく出る。最終的な効果は壁時計の A/B で語ること。
            double GetParallelSpeedup() const
            {
                return activeSpanMs > 0.0 ? totalBusyMs / activeSpanMs : 0.0;
            }
        };

        /// @brief 現在の統計を取得
        Stats GetStats() const;

        /// @brief 直近に完了したタスクの記録を古い順に取得
        std::vector<TaskRecord> GetRecentTasks() const;

        /// @brief 統計と履歴をリセットし、計測開始時点を今にする
        /// @note 「起動中の集計」と「ゲーム中の集計」を混ぜないために必要。
        ///       混ざると起動時の巨大な値に引きずられて現在の状態が読めない。
        void ResetStats();

    private:
        struct Task
        {
            std::function<void()> fn;
            std::string           label;
            std::chrono::steady_clock::time_point enqueuedAt;
        };

        void WorkerLoop(uint32_t workerIndex);

        /// @brief ワーカーを起動する（両コンストラクタの共通処理）
        void Launch(const ThreadPoolDesc& desc);

        /// @brief タスク 1 件を実行し、統計を記録する
        void RunTask(Task&& task, uint32_t workerIndex);

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

        // pendingTasks を atomic で持つことで GetStats が queueMutex_ を取らずに済む。
        // UI は毎フレーム GetStats を呼ぶので、ここでワーカーと競合させたくない。
        std::atomic<size_t>   pendingCount_{ 0 };
        std::atomic<uint32_t> activeTasks_{ 0 };
        std::atomic<uint64_t> totalSubmitted_{ 0 };
        std::atomic<uint64_t> totalCompleted_{ 0 };

        // ─── 統計（statsMutex_ が守る）────────────────────────────
        // ワーカーはタスクの開始と終了で 2 回だけこのロックを取る。
        // タスク本体はロックの外で走るので、粒度が ms 級なら競合は無視できる。
        mutable std::mutex statsMutex_;
        std::chrono::steady_clock::time_point epoch_;
        std::array<WorkerState, kMaxWorkers>  workerStates_{};

        // 実行中タスクの開始時刻。WorkerState に持たせず内部に隔離してあるのは、
        // 公開構造体に time_point を晒すと利用側が epoch_ を知らずに差を取れないため。
        // 経過時間の計算は GetStats が行い、外へは ms だけを渡す。
        std::array<std::chrono::steady_clock::time_point, kMaxWorkers> workerTaskStart_{};
        std::vector<TaskRecord> recentTasks_;
        size_t   recentHead_ = 0;
        double   totalBusyMs_ = 0.0;
        double   totalWaitMs_ = 0.0;

        // 稼働区間（最初のタスク開始 〜 最後のタスク終了）。epoch_ からの相対 ms。
        // 占有率と直列比の分母。詳細は Stats::GetOccupancy の note を参照
        bool     hasActivity_ = false;
        double   firstTaskStartMs_ = 0.0;
        double   lastTaskEndMs_ = 0.0;
        double   maxWaitMs_ = 0.0;
        double   maxRunMs_ = 0.0;
        uint32_t peakActiveTasks_ = 0;
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
        task.enqueuedAt = std::chrono::steady_clock::now();

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
