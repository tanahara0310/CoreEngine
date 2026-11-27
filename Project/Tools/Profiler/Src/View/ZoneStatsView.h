#pragma once

#include "App/ViewState.h"
#include "Capture/ZoneKind.h"

#include <cstdint>
#include <string>
#include <vector>

namespace tracy
{
    class Worker;
}

/// @file ZoneStatsView.h
/// @brief 区間の統計の表（区間の種類ごとの回数・合計・平均・最大・ばらつき）と、選んだ行の 1 回ごとの時間の分布

namespace CoreEngine::Profiler
{
    class FrameSnapshot;

    /// @brief 表の 1 行（区間の種類 1 つ分）
    struct ZoneStatRow
    {
        int16_t srcloc = 0;    ///< 区間の場所の番号
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

        /// @brief 鍵をかけた受け取り役から、選んだ行の 1 回ごとの時間を写して分布にする
        void UpdateSelection(const tracy::Worker& worker, const FrameSnapshot& frames, const ViewState& view, bool live, double now);

        /// @brief 表と、選んだ行の分布を描く。「最大のフレームを開く」は view へ書く
        void Draw(const FrameSnapshot& frames, ViewState& view);

        /// @brief 名前で絞り込み、種類の絞り込みを外す（gpu なら GPU の表にする）
        void ShowName(const std::string& name, bool gpu);

    private:
        /// @brief 選んだ行の分布（時間はナノ秒）
        struct Distribution
        {
            bool valid = false;
            bool gpu = false;
            int16_t srcloc = 0;
            std::string name;
            std::string description;
            std::string location;
            uint64_t count = 0;
            int64_t min = 0;
            int64_t max = 0;
            int64_t median = 0;
            int64_t target = 0;        ///< 数えたときの目標
            uint64_t overTarget = 0;   ///< 目標を超えた回
            int firstDecade = 0;       ///< 横の左端（ナノ秒の 10 の累乗の指数）
            int decades = 1;
            std::vector<uint32_t> bins;
            ZoneRef maxRef;            ///< いちばん長かった 1 回
            size_t maxFrame = 0;
        };

        void CopyRows(const tracy::Worker& worker, bool copyCpu, bool copyGpu);
        void SortRows(std::vector<ZoneStatRow>& rows) const;
        void RebuildVisible();
        void DrawToolbar();
        void DrawTable(float panelHeight);
        void DrawDistribution(const FrameSnapshot& frames, ViewState& view);
        void DrawHistogram();

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

        bool hasSelection_ = false;
        bool selectionGpu_ = false;
        int16_t selectionSrcloc_ = 0;
        Distribution distribution_;
        double lastDistribution_ = -1.0;
        float infoHeight_ = 0.0f; ///< 分布の欄の左側の中身の高さ（前のフレームで測った値）
    };
}
