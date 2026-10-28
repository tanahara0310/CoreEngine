#pragma once

#include "IBroadPhase.h"

namespace CoreEngine
{
namespace BroadPhase
{
    /// @brief 全ペア総当たり
    /// @details 件数が少ないうちはこれが最速（空間分割の構築コストが無い）。
    ///          他実装の正しさを比較する基準にもなる。
    ///          静止同士は候補にならないので、静止したものには動くものだけを当てる
    ///          （静止したものがいくつあっても、手間は動くものの数に比例する）。
    class BruteForceBroadPhase : public IBroadPhase {
    public:
        const char* GetName() const override { return "BruteForce"; }

        void Clear() override;
        uint32_t Insert(const Proxy& proxy) override;
        void CollectPairs(std::vector<PairIndices>& outPairs) override;

    private:
        std::vector<Proxy> proxies_;
        std::vector<uint32_t> moving_;  ///< 静止していないものの登録インデックス（昇順）
    };
}
}
