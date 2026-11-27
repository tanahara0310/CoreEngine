#pragma once

#include "App/ViewState.h"
#include "Capture/ZoneKind.h"
#include "TracyWorker.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/// @file ZoneDetailPanel.h
/// @brief 選んだ区間の欄（このフレームの時間・平均・最大とそのフレーム・ばらつき・回数・中身）

namespace CoreEngine::Profiler
{
    class FrameSnapshot;
    class SourceInfoCache;

    class ZoneDetailPanel
    {
    public:
        /// @brief 子の区間（同じ名前はまとめる）
        struct Child
        {
            int16_t srcloc = 0;
            std::string name;
            int64_t time = 0;
            uint32_t calls = 0;
        };

        /// @brief 写した中身を捨てる（記録を差し替えたとき）
        void Reset();

        /// @brief 鍵をかけた受け取り役から、選んだ区間の数字を写す
        /// @details 選んだフレームが変わり、選んだ区間がそのフレームに無ければ、同じ区間をそのフレームから探して選び直す
        void Update(const tracy::Worker& worker, const FrameSnapshot& frames, SourceInfoCache& sources, ViewState& view,
            bool live, double now);

        /// @brief 欄を描く。「そのフレームへ」「統計で見る」は view へ書く
        void Draw(const FrameSnapshot& frames, ViewState& view);

    private:
        struct Detail
        {
            bool valid = false;
            bool gpu = false;
            bool statsReady = false;
            std::string name;
            std::string description;  ///< 説明の辞書の「何をしているか」
            std::string location;     ///< ファイル名:行
            std::string place;        ///< メインスレッド・ワーカー・GPU
            ZoneKind kind = ZoneKind::Other;

            int64_t duration = 0;     ///< 選んだ 1 回の時間
            size_t frame = 0;         ///< 選んだ 1 回が始まったフレーム
            int64_t frameDuration = 0;
            int64_t frameTotal = 0;   ///< そのフレームの中の同じ区間の合計
            uint32_t frameCalls = 0;

            uint64_t count = 0;
            int64_t total = 0;
            int64_t max = 0;
            double stddev = 0.0;
            ZoneRef maxRef;           ///< いちばん長かった 1 回
            size_t maxFrame = 0;
            std::string maxChildName; ///< いちばん長かった 1 回の中で、時間の大半を占めていた区間
            int64_t maxChildTime = 0;

            std::vector<Child> children;
            int64_t self = 0;         ///< 子のどれにも入らない時間
        };

        void Retarget(const tracy::Worker& worker, const FrameSnapshot& frames, ViewState& view);
        void CollectCpu(const tracy::Worker& worker, const FrameSnapshot& frames, SourceInfoCache& sources, const ZoneRef& ref);
        void CollectGpu(const tracy::Worker& worker, const FrameSnapshot& frames, SourceInfoCache& sources, const ZoneRef& ref);
        void DrawNumbers(const FrameSnapshot& frames, const ViewState& view);
        void DrawChildren();

        Detail detail_;
        ZoneRef shown_;
        size_t checkedFrame_ = static_cast<size_t>(-1); ///< 選び直しを確かめた最後のフレーム
        double lastCopy_ = -1.0;
    };
}
