#pragma once

#include "App/ViewState.h"
#include "Capture/ZoneKind.h"
#include "TracyWorker.hpp"

#include <cstdint>
#include <string>
#include <vector>

/// @file TimelineView.h
/// @brief タイムライン（メインスレッド・ワーカー・GPU のキューの区間を時間の横軸に並べる）

namespace CoreEngine::Profiler
{
    class FrameSnapshot;
    class SourceInfoCache;

    class TimelineView
    {
    public:
        /// @brief 写した中身を捨てる（記録を差し替えたとき）
        void Reset();

        /// @brief 鍵をかけた受け取り役から、見ている範囲に入る区間を写す
        void Update(const tracy::Worker& worker, const FrameSnapshot& frames, SourceInfoCache& sources, const ViewState& view);

        /// @brief タイムラインを描き、拡大・移動・選んだ区間を view へ書く
        void Draw(const FrameSnapshot& frames, const SourceInfoCache& sources, ViewState& view);

    private:
        /// @brief 行（スレッドか GPU のキュー 1 つ）
        enum class RowKind : uint8_t
        {
            Main,
            Worker,
            Gpu,
        };

        /// @brief 写した区間 1 つ（狭い区間をまとめたものも 1 つ）
        struct Zone
        {
            int64_t start = 0;
            int64_t end = 0;
            ZoneRef ref;          ///< まとめた区間では空
            int16_t srcloc = 0;
            uint16_t lane = 0;    ///< 行の中の段（深さ）
            uint16_t row = 0;
            uint32_t merged = 1;  ///< まとめた区間の数（1 ならまとめていない）
            bool odd = false;     ///< 隣と色を少し変える
        };

        struct Row
        {
            RowKind kind = RowKind::Main;
            std::string title;
            std::string subtitle;
            uint16_t lanes = 0;   ///< 段の数（0 なら見ている範囲に区間が無い）
        };

        /// @brief マウスを重ねた区間の統計（1 フレーム遅れで写す）
        struct HoverStats
        {
            int16_t srcloc = 0;
            bool gpu = false;
            bool valid = false;
            uint64_t count = 0;
            int64_t total = 0;
            int64_t max = 0;
        };

        void CollectCpu(const tracy::Worker& worker, SourceInfoCache& sources,
            const tracy::Vector<tracy::short_ptr<tracy::ZoneEvent>>& events, uint16_t row, uint16_t laneBase, int depth,
            uint64_t thread, uint16_t& lanes);
        void CollectGpu(const tracy::Worker& worker, SourceInfoCache& sources,
            const tracy::Vector<tracy::short_ptr<tracy::GpuEvent>>& events, uint16_t row, int depth, uint64_t context,
            uint16_t& lanes);
        void FlushMerge();
        void CopyHoverStats(const tracy::Worker& worker);

        void DrawHeader(const FrameSnapshot& frames, ViewState& view);
        void DrawRuler(ImDrawList* drawList, const FrameSnapshot& frames, const ViewState& view, float left, float top,
            float width, float height, float labelWidth);
        void HandleInput(const FrameSnapshot& frames, ViewState& view, float left, float width, const Zone* hovered);
        void DrawTooltip(const Zone& zone, const SourceInfoCache& sources, const FrameSnapshot& frames);
        void DrawLegend();

        std::vector<Zone> zones_;
        std::vector<Row> rows_;
        Zone merge_;
        bool merging_ = false;

        int64_t rangeStart_ = 0;   ///< 写したときの範囲
        int64_t rangeEnd_ = 0;
        int64_t minZoneNs_ = 0;    ///< これより短い区間はまとめる
        int64_t captureStart_ = 0;
        int64_t captureEnd_ = 0;
        int64_t gpuFrameTime_ = -1; ///< 選んだフレームの間に始まった GPU の区間の合計
        uint32_t workerThreads_ = 0;
        float plotWidth_ = 800.0f;  ///< 前のフレームで描いた横幅（写すときの細かさに使う）

        HoverStats hoverStats_;
        int16_t hoverSrcloc_ = 0;
        bool hoverGpu_ = false;
        bool hasHover_ = false;
    };
}
