#pragma once

#include "Capture/UpdateTree.h"

#include <cstdint>
#include <utility>
#include <vector>

/// @file UpdateTreeView.h
/// @brief Update の内訳のタブ（段の順の木の表・小さな区間のまとめ・選んだ行のフレームごとの推移）

namespace CoreEngine::Profiler
{
    class FrameSnapshot;
    struct ViewState;

    class UpdateTreeView
    {
    public:
        /// @brief 開いた行と選んだ行を捨てる（記録を差し替えたとき）
        void Reset();

        /// @brief タブの中身を描く。推移を写す行は tree へ頼む（次のフレームで写る）
        void Draw(UpdateTree& tree, const FrameSnapshot& frames, ViewState& view);

    private:
        enum class SortMode
        {
            Tree, ///< 段の順（木）
            Mean, ///< 平均の重い順
            Max,  ///< 最大の重い順
        };

        enum class RowKind
        {
            Node,  ///< 区間
            Self,  ///< 区間の「自分だけ」（node は親の区間）
            Group, ///< 小さな区間をまとめた「ほか n 個」（node は親の区間）
        };

        /// @brief 表の 1 行の数字（時間はナノ秒）
        struct RowValues
        {
            double mean = 0.0;
            int64_t max = 0;
            const tracy::ZoneEvent* maxZone = nullptr;
            size_t maxFrame = 0;
            uint32_t spikes = 0;
            int64_t selected = -1; ///< 選んだフレームの時間（写していなければ -1）
        };

        struct Row
        {
            RowKind kind = RowKind::Node;
            uint32_t node = UpdateTree::kNoNode;
            uint32_t depth = 0;
            UpdateNodeSummary summary;
            RowValues values;
            std::vector<std::pair<uint32_t, bool>> members; ///< Group にまとめた行（節と、自分だけか）
            bool hasToggle = false;
            bool open = false;
        };

        void BuildRows(const UpdateTree& tree);
        void AddTreeRows(const UpdateTree& tree, uint32_t node, uint32_t depth, double rootMean, bool filtering);
        void AddFlatRows(const UpdateTree& tree);
        Row MakeRow(const UpdateTree& tree, RowKind kind, uint32_t node, uint32_t depth) const;
        Row MakeGroup(const UpdateTree& tree, uint32_t parent, uint32_t depth, const std::vector<std::pair<uint32_t, bool>>& members) const;
        bool IsSmall(const Row& row) const;
        bool Matches(const UpdateTree& tree, uint32_t node) const;
        bool SubtreeMatches(const UpdateTree& tree, uint32_t node) const;
        bool IsOpen(uint32_t node, double mean, double rootMean) const;
        bool FilterActive() const { return filter_[0] != '\0'; }

        void DrawToolbar(const UpdateTree& tree, const FrameSnapshot& frames, ViewState& view);
        void DrawTable(const UpdateTree& tree, const FrameSnapshot& frames, const ViewState& view, float height);
        void DrawRow(const UpdateTree& tree, const Row& row, double rootMean, const ViewState& view);
        void DrawSelection(UpdateTree& tree, const FrameSnapshot& frames, ViewState& view);
        void DrawTrend(const UpdateTree& tree, const FrameSnapshot& frames, ViewState& view, const Row& row);
        void Toggle(const Row& row);

        SortMode sortMode_ = SortMode::Tree;
        bool mergeSmall_ = true;
        bool includeStopped_ = false;
        char filter_[128] = {};

        std::vector<int8_t> expanded_;   ///< 節ごとの開閉（-1 は既定のまま）
        std::vector<uint8_t> groupOpen_; ///< 節ごとの「ほか n 個」の開閉
        bool flatGroupOpen_ = false;     ///< 重い順のときの「ほか n 個」の開閉
        std::vector<Row> rows_;

        uint32_t selectedNode_ = UpdateTree::kRoot;
        bool selectedSelf_ = false;
        float infoHeight_ = 0.0f; ///< 選んだ行の欄の左側の中身の高さ（前のフレームで測った値）
    };
}
