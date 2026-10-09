#pragma once
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "IStartupTask.h"

namespace CoreEngine
{
    /// @brief 起動処理を「1 ステップずつ進められる列」として保持する
    /// @details 呼び出し側が Step() の合間にメッセージポンプとスプラッシュ描画を挟めるようにする。
    ///          各ステップの CPU 時間はログへ残すので、起動時間の回帰を毎起動そのまま追える。
    /// @warning 実行開始後にステップを追加しないこと（内部 vector の再確保で実行中の参照が壊れる）。
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

        /// @brief 次のステップを 1 回だけ呼び、CPU 時間を記録する（済んだらログへ出して次へ進む）
        /// @return Pending ならステップは進んでいない（次の Step でもう一度呼ぶ）
        StartupTaskResult Step();

        /// @brief 次に実行するステップの表示名（残っていなければ空文字）
        std::string GetNextLabel() const;

        /// @brief 完了率 0.0〜1.0（待っているステップの進み具合を含む）
        /// @details 前回の時間を読めていれば、各ステップを前回かかった時間の割合で数える。
        ///          読めていなければステップの数で割る。
        float GetProgress() const;

        /// @brief 前回の起動で記録した各ステップの時間を読む（進捗の重みに使う）
        /// @note ステップの数が同じ記録だけを使う。ログの初期化より前に呼ぶので、読めなくても何も出さない
        void LoadTimings(const std::filesystem::path& path);

        /// @brief 今回の各ステップの時間を書く（ステップの数が違う記録はそのまま残す）
        void SaveTimings(const std::filesystem::path& path) const;

        size_t GetCompletedCount() const { return cursor_; }
        size_t GetTotalCount() const { return tasks_.size(); }

        /// @brief 全ステップの合計壁時計時間（秒）
        double GetTotalSeconds() const { return totalSeconds_; }

        /// @brief 全ステップの合計 CPU 時間（秒）。壁時計との差が「待ち」
        double GetTotalCpuSeconds() const { return totalCpuSeconds_; }

        /// @brief 遅い順の内訳と合計をログへ出す（起動完了時に 1 回）
        void LogSummary() const;

    private:
        /// @brief ステップ 1 つ分（タスク本体と実測時間）
        struct Entry {
            std::unique_ptr<IStartupTask> task;
            std::string executedLabel;   // 実行時点で確定した表示名（サマリ用）
            double seconds = 0.0;      // 壁時計（待っていたあいだを含む）
            double cpuSeconds = 0.0;   // 実行スレッドが実際に CPU を使った時間
            int calls = 0;             // Execute を呼んだ回数（待ちのステップは 2 回以上）
            std::chrono::steady_clock::time_point firstCall{};
        };

        std::vector<Entry> tasks_;
        std::vector<double> estimates_;   // 前回の各ステップの時間（秒）。空なら重みを使わない
        double estimateTotal_ = 0.0;
        size_t cursor_ = 0;
        double totalSeconds_ = 0.0;
        double totalCpuSeconds_ = 0.0;
    };
}
