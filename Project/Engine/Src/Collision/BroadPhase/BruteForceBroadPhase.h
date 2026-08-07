#pragma once

#include "IBroadPhase.h"

namespace CoreEngine
{
namespace BroadPhase
{
    /// @brief 全ペア総当たり
    /// @details 件数が少ないうちはこれが最速（空間分割の構築コストが無い）。
    ///          他実装の正しさを比較する基準にもなる。
    class BruteForceBroadPhase : public IBroadPhase {
    public:
        const char* GetName() const override { return "BruteForce"; }

        void Clear() override;
        uint32_t Insert(const Proxy& proxy) override;
        void CollectPairs(std::vector<PairIndices>& outPairs) override;

    private:
        std::vector<Proxy> proxies_;
    };
}
}
