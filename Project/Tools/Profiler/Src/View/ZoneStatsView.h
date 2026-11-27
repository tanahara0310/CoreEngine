#pragma once

#include "Capture/ZoneKind.h"

#include <cstdint>
#include <string>
#include <vector>

namespace tracy
{
    class Worker;
}

/// @file ZoneStatsView.h
/// @brief 区間の統計の表（区間の種類ごとの回数・合計・平均・最大・ばらつき）

namespace CoreEngine::Profiler
{
    /// @brief 表の 1 行（区間の種類 1 つ分）
    struct ZoneStatRow
    {
        std::string name;      ///< 区間の名前
        std::string file;      ///< 場所のファイル（フルパス）
        std::string location;  ///< 場所（ファイル名:行）
        uint32_t line = 0;
        ZoneKind kind = ZoneKind::Other;
        uint64_t count = 0;
        int64_t total = 0;     ///< 合計（ナノ秒）
        int64_t max = 0;       ///< 最大（ナノ秒）
        double mean = 0.0;     ///< 1 回あたり（ナノ秒）
        double perFrame = 0.0; ///< 1 フレームあたり（ナノ秒）
        double stddev = 0.0;   ///< ばらつき（標準偏差・ナノ秒）
    };

    class ZoneStatsView
    {
    public:
        /// @brief 写した中身を捨てる（記録を差し替えたとき）
        void Reset();

        /// @brief 鍵をかけた受け取り役から、要るときだけ統計を写す
        /// @param live 受け取りの最中なら true（一定の間隔で写し直す）
        /// @param now 秒の時計（ImGui::GetTime）
        void Update(const tracy::Worker& worker, bool live, double now);

        /// @brief 表を描く
        void Draw();

    private:
        void CopyRows(const tracy::Worker& worker, bool copyCpu, bool copyGpu);
        void SortRows(std::vector<ZoneStatRow>& rows) const;
        void RebuildVisible();
        void DrawToolbar();
        void DrawTable();

        std::vector<ZoneStatRow> cpuRows_;
        std::vector<ZoneStatRow> gpuRows_;
        std::vector<const ZoneStatRow*> visible_; ///< 絞り込みの後の行（並べた順）

        bool cpuCopied_ = false;
        bool gpuCopied_ = false;
        bool wasLive_ = false;
        bool waitingForStatistics_ = false;
        double lastCopyTime_ = -1.0;
        uint64_t frameCount_ = 0;

        bool showGpu_ = false;
        int kindFilter_ = -1;      ///< -1 = すべて。それ以外は ZoneKind の番号
        char nameFilter_[128] = {};
        bool needsSort_ = true;
        bool needsVisible_ = true;
        int sortColumn_ = 3;       ///< 並べている列（既定は合計）
        bool sortAscending_ = false;
    };
}
