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
    /// @details 答えるのは「ワーカーを遊ばせていないか」「いま何が走っているか」
    ///          「長い棒はどれか」の 3 点。
    /// @note 集計元は ThreadPool の直近タスクのリングバッファなので、それより古い分は入らない。
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
        /// @brief 表示対象のスレッドプール 1 つ分（取得元と表示用の履歴バッファ）
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
