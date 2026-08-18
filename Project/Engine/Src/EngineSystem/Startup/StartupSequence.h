#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "IStartupTask.h"

namespace CoreEngine
{
    /// @brief 起動処理を「1 ステップずつ進められる列」として保持する。
    ///
    /// @details 従来は EngineSystem::Initialize が全部を一息に実行していたため、
    ///          その間メッセージポンプが一度も回らず、表示済みの全画面ウィンドウが
    ///          Windows から「応答なし」と判定されていた（起動が何秒でも起きる）。
    ///          呼び出し側は Step() の合間に ProcessMessage とスプラッシュ描画を挟む。
    ///
    ///          各ステップの CPU 時間を計測してログへ残すので、
    ///          「どのステップが何秒か」を毎起動そのまま追える（起動時間の回帰検知）。
    ///
    /// @warning 実行を始めた後にステップを追加してはいけない。内部 vector が
    ///          再確保されると、Step() が保持している実行中エントリの参照が壊れる。
    ///          ステップは全部積んでから回すこと。
    class StartupSequence {
    public:
        /// @brief ラムダをステップとして末尾に積む
        void Add(std::string label, std::function<void()> action);

        /// @brief 表示名を実行直前に決めるステップを末尾に積む
        void Add(std::function<std::string()> labelProvider, std::function<void()> action);

        /// @brief 任意の IStartupTask を末尾に積む
        void Add(std::unique_ptr<IStartupTask> task);

        /// @brief 未実行のステップが残っているか
        bool HasNext() const { return cursor_ < tasks_.size(); }

        /// @brief 次のステップを 1 つだけ実行し、CPU 時間を記録してログへ出す
        void Step();

        /// @brief 次に実行するステップの表示名（残っていなければ空文字）
        std::string GetNextLabel() const;

        /// @brief 完了率 0.0〜1.0
        float GetProgress() const;

        size_t GetCompletedCount() const { return cursor_; }
        size_t GetTotalCount() const { return tasks_.size(); }

        /// @brief 全ステップの合計壁時計時間（秒）
        double GetTotalSeconds() const { return totalSeconds_; }

        /// @brief 全ステップの合計 CPU 時間（秒）。壁時計との差が「待ち」
        double GetTotalCpuSeconds() const { return totalCpuSeconds_; }

        /// @brief 遅い順の内訳と合計をログへ出す（起動完了時に 1 回）
        void LogSummary() const;

    private:
        struct Entry {
            std::unique_ptr<IStartupTask> task;
            std::string executedLabel;   // 実行時点で確定した表示名（サマリ用）
            double seconds = 0.0;      // 壁時計
            double cpuSeconds = 0.0;   // 実行スレッドが実際に CPU を使った時間
        };

        std::vector<Entry> tasks_;
        size_t cursor_ = 0;
        double totalSeconds_ = 0.0;
        double totalCpuSeconds_ = 0.0;
    };
}
