#pragma once

#include "App/ViewState.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tracy
{
    class Worker;
    struct MessageData;
}

/// @file LogView.h
/// @brief ログのタブ（エンジンが Tracy へ送った警告とエラーを、時刻・フレームつきで並べる）

namespace CoreEngine::Profiler
{
    class FrameSnapshot;

    /// @brief ログの 1 行の重さ
    enum class LogLevel : uint8_t
    {
        Other,   ///< 重さを持たない文（重さを送る前のエンジンの記録など）
        Warning,
        Error,
    };

    /// @brief ログの 1 行（時刻はナノ秒）
    struct LogEntry
    {
        int64_t time = 0;
        LogLevel level = LogLevel::Other;
        bool fromTracy = false; ///< Tracy 自身が送った文
        uint64_t threadId = 0;
        std::string thread;     ///< 画面に出すスレッドの名前
        std::string text;
    };

    class LogView
    {
    public:
        /// @brief 写した中身を捨てる（記録を差し替えたとき）
        void Reset();

        /// @brief 鍵をかけた受け取り役から、増えた文を写す。スレッドの名前は届き終えるまで引き直す
        void Update(const tracy::Worker& worker, const FrameSnapshot& frames);

        /// @brief 表を描く。行を選ぶと view のフレームを選び、ダブルクリックでタイムラインへ切り替える
        void Draw(const FrameSnapshot& frames, ViewState& view, bool live);

        /// @brief タブの名前（数があれば足す）
        std::string TabLabel() const;

    private:
        void CopyFrom(const tracy::Worker& worker, size_t first);
        void NameThreads(const tracy::Worker& worker);
        void RebuildVisible();
        bool Matches(const LogEntry& entry) const;
        void DrawToolbar();
        void DrawTable(const FrameSnapshot& frames, ViewState& view, bool live);

        std::vector<LogEntry> entries_;
        std::vector<size_t> visible_;               ///< 絞り込みの後の行（entries_ の番号）
        const tracy::MessageData* lastCopied_ = nullptr; ///< 最後に写した文（途中へ差し込まれたら全部写し直す）
        std::vector<size_t> unnamed_;               ///< スレッドの名前がまだ届いていない行（entries_ の番号）
        uint64_t mainThreadId_ = 0;
        int64_t firstTime_ = 0;
        size_t warningCount_ = 0;
        size_t errorCount_ = 0;
        size_t otherCount_ = 0;

        int levelFilter_ = -1;       ///< -1 = すべて。それ以外は LogLevel の番号
        char textFilter_[128] = {};
        bool needsVisible_ = true;
        bool hasSelection_ = false;
        size_t selectedEntry_ = 0;
        size_t shownCount_ = 0;      ///< 前のフレームに描いた行の数（増えたときにいちばん下を見ていれば下へ送る）
    };
}
