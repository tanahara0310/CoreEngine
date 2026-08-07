#pragma once

#include "Math/Geometry/Shapes.h"

#include <cstdint>
#include <utility>
#include <vector>

/// @file
/// @brief ブロードフェーズ（候補ペアの絞り込み）の共通インタフェース
/// @details **Collider を知らない**純データ設計にしてある。合成した proxy を入れて
///          候補ペアを取り出すだけなので、GameObject を作らずに単体テストできる
///          （実装差し替え時に「同じペア集合を返すか」を機械的に比較できる）。

namespace CoreEngine
{
namespace BroadPhase
{
    /// @brief ブロードフェーズに登録する 1 件ぶんの情報
    struct Proxy {
        Geometry::AABB bounds{};        ///< ワールド空間の外接 AABB
        uint64_t       layerMask = 0;   ///< 衝突しうる相手レイヤーのビット集合
        uint32_t       layerIndex = 0;  ///< 自分のレイヤー番号
        uint64_t       ownerId = 0;     ///< 同一オーナー間を弾くための識別子（0 = 判定しない）
        bool           isStatic = false;///< 動かないか（静止同士は候補から外す）
    };

    /// @brief 候補ペア（Insert した順のインデックス。常に first < second）
    using PairIndices = std::pair<uint32_t, uint32_t>;

    /// @brief 候補ペア絞り込みの共通インタフェース
    class IBroadPhase {
    public:
        virtual ~IBroadPhase() = default;

        /// @brief 実装名（デバッグ表示・ログ用）
        virtual const char* GetName() const = 0;

        /// @brief 登録内容をすべて捨てる
        virtual void Clear() = 0;

        /// @brief proxy を 1 件登録する
        /// @return 登録インデックス（CollectPairs が返すのはこの値）
        virtual uint32_t Insert(const Proxy& proxy) = 0;

        /// @brief 候補ペアを列挙する
        /// @param outPairs 追記先（呼び出し側で clear 済みであること）
        /// @note レイヤーマスク・同一オーナー・静止同士・AABB 重なりで絞り込む。
        ///       ここを通ったペアだけがナローフェーズ（実形状の判定）へ進む。
        virtual void CollectPairs(std::vector<PairIndices>& outPairs) = 0;
    };

    /// @brief 2 つの proxy が候補になりうるか（実装間で共通の足切り条件）
    /// @details 各実装がここを呼ぶことで、絞り込み条件の食い違いを防ぐ。
    inline bool ShouldTestPair(const Proxy& a, const Proxy& b)
    {
        // 静止同士は永久に動かないので判定しない
        if (a.isStatic && b.isStatic) { return false; }

        // 同じオーナーが持つコライダー同士は判定しない
        if (a.ownerId != 0 && a.ownerId == b.ownerId) { return false; }

        // レイヤーマトリクスによる足切り（ビット演算 1 回）
        if (((a.layerMask >> b.layerIndex) & 1u) == 0) { return false; }

        return true;
    }
}
}
