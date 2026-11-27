#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tracy
{
    class Worker;
    struct ThreadData;
}

/// @file FrameSnapshot.h
/// @brief 鍵をかけた受け取り役から、フレームの始まりと終わり・メインスレッドの待ちの時間を写して持つ

namespace CoreEngine::Profiler
{
    class SourceInfoCache;

    /// @brief フレーム 1 つ分（時刻はナノ秒）
    struct FrameTime
    {
        int64_t begin = 0;
        int64_t end = 0;
        int64_t wait = -1; ///< メインスレッドの待ちの合計。まだ数えていなければ -1

        int64_t Duration() const { return end - begin; }
        int64_t Work() const { return wait < 0 ? Duration() : Duration() - wait; }
    };

    class FrameSnapshot
    {
    public:
        /// @brief 写した中身を捨てる（記録を差し替えたとき）
        void Reset();

        /// @brief 増えたフレームと、まだ伸びている最後のフレームを写す。メインスレッドもここで探す
        /// @param live 受け取りの最中なら true（最後のフレームはまだ終わっていない）
        void UpdateFrames(const tracy::Worker& worker, bool live);

        /// @brief [first, last) のフレームのうち、まだ数えていない分の待ちを数える
        void UpdateWaits(const tracy::Worker& worker, SourceInfoCache& sources, size_t first, size_t last);

        size_t Count() const { return frames_.size(); }
        const FrameTime& At(size_t index) const { return frames_[index]; }

        /// @brief 画面に出すフレームの番号（元の Tracy の画面と同じ数え方）
        uint64_t Number(size_t index) const;

        /// @brief time を含むフレームの番号。記録より前なら 0、後なら最後のフレーム
        size_t FindFrame(int64_t time) const;

        /// @brief 終わったフレームか（受け取りの最中の最後のフレームは終わっていない）
        bool IsComplete(size_t index) const { return !live_ || index + 1 < frames_.size(); }

        /// @brief 受け取りの最中か
        bool IsLive() const { return live_; }

        /// @brief 最後の終わったフレームの番号（フレームが無ければ 0）
        size_t LastComplete() const;

        /// @brief メインスレッド（中身は鍵をかけている間だけ読む）。見つかっていなければ nullptr
        const tracy::ThreadData* MainThread() const { return mainThread_; }

    private:
        void FindMainThread(const tracy::Worker& worker);

        std::vector<FrameTime> frames_;
        uint64_t frameOffset_ = 0;
        const tracy::ThreadData* mainThread_ = nullptr;
        bool mainThreadByName_ = false;
        bool live_ = false;
    };
}
