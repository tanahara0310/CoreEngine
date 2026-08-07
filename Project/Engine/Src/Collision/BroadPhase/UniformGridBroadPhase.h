#pragma once

#include "IBroadPhase.h"

#include <unordered_map>

namespace CoreEngine
{
namespace BroadPhase
{
    /// @brief 一様グリッド（ハッシュ空間分割）
    /// @details AABB が重なるセルすべてに proxy を登録し、セル単位でペアを列挙する。
    ///          セル境界をまたぐ物体は複数セルに入るため、同じペアが複数回出る。
    ///          重複は列挙後にまとめて除去する。
    ///
    ///          **セルサイズの目安**: よくある物体サイズの 1〜2 倍。小さすぎると
    ///          1 物体が多数のセルに入って登録コストが跳ね、大きすぎると
    ///          1 セルに集中して総当たりに近づく。
    class UniformGridBroadPhase : public IBroadPhase {
    public:
        explicit UniformGridBroadPhase(float cellSize = 8.0f);

        const char* GetName() const override { return "UniformGrid"; }

        void Clear() override;
        uint32_t Insert(const Proxy& proxy) override;
        void CollectPairs(std::vector<PairIndices>& outPairs) override;

        /// @brief セルサイズを設定する（次の Clear 以降に効く）
        void SetCellSize(float cellSize);
        float GetCellSize() const { return cellSize_; }

        /// @brief 1 つの proxy が跨ぐセル数の上限
        /// @details 巨大な AABB（地形など）が何万セルにも展開されるのを防ぐ保険。
        ///          超えた proxy は「常に候補」として扱い、全 proxy と突き合わせる。
        static constexpr uint32_t kMaxCellsPerProxy = 512;

    private:
        struct CellCoord {
            int x, y, z;
            bool operator==(const CellCoord& rhs) const {
                return x == rhs.x && y == rhs.y && z == rhs.z;
            }
        };

        struct CellHash {
            size_t operator()(const CellCoord& c) const noexcept;
        };

        /// @brief 座標をセル添字へ
        CellCoord ToCell(float x, float y, float z) const;

        float cellSize_ = 8.0f;
        float invCellSize_ = 1.0f / 8.0f;

        std::vector<Proxy> proxies_;
        std::unordered_map<CellCoord, std::vector<uint32_t>, CellHash> cells_;

        /// セル数上限を超えた（＝どのセルにも入れなかった）proxy
        std::vector<uint32_t> oversized_;
    };
}
}
