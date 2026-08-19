#pragma once

#ifdef USE_IMGUI

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace CoreEngine
{
    class ThreadPool;

    /// @brief スレッドプールの動作状態を可視化するデバッグ UI パネル
    ///
    /// @details **このパネルが答えるべき質問は 3 つだけ。**
    ///          ① ワーカーを遊ばせていないか（占有率・直列比）
    ///          ② いま何が走っているか（ワーカーごとのタスク名）
    ///          ③ 長い棒はどれか（タスク種別ごとの合計と、実行時間順の一覧）
    ///          旧版は「W3 が緑」までしか言えず、①も③も答えられなかった。
    ///          色の凡例より数字とタスク名を出すほうが情報量が多い。
    ///
    /// @note 集計は ThreadPool が持つ直近タスクのリングバッファ
    ///       （ThreadPool::kRecentTaskCapacity 件）から作る。それより古い
    ///       タスクは合計に入らないので、件数はヘッダに明示する。
    class ThreadProfilerUI
    {
    public:
        static constexpr int kHistorySize = 180;

        /// @brief スレッドプールをプロファイラに登録
        /// @param name   表示名
        /// @param getter プールを返す関数。未生成の間は nullptr を返してよい
        void RegisterPool(const std::string& name, std::function<ThreadPool*()> getter);

        /// @brief ImGui パネルを描画
        void Draw();

    private:
        struct PoolEntry
        {
            std::string name;
            std::function<ThreadPool*()> getter;

            std::array<float, kHistorySize> activeHistory = {};
            std::array<float, kHistorySize> pendingHistory = {};
            int      historyOffset = 0;
            uint32_t peakActive = 0;

            /// @brief 表の折り畳み状態（既定は閉じて縦を節約する）
            bool showGroups = true;
            bool showTasks = false;
        };

        /// @brief プロセス全体のワーカー枠の使用状況を出す
        void DrawBudgetSummary();

        void DrawPool(PoolEntry& entry);

        /// @brief ワーカーごとの状態表（いま何が走っているか）
        void DrawWorkerTable(const ThreadPool& pool);

        /// @brief タスク種別（ラベルの ':' より前）ごとの集計表
        void DrawGroupTable(const ThreadPool& pool);

        /// @brief 直近タスクの一覧（実行時間順にソート可能）
        void DrawTaskTable(const ThreadPool& pool);

        std::vector<PoolEntry> pools_;
    };

} // namespace CoreEngine

#endif // USE_IMGUI
