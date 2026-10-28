#include "pch.h"
#include "BruteForceBroadPhase.h"
#include "Math/Geometry/Intersect.h"

#include <algorithm>

namespace CoreEngine
{
namespace BroadPhase
{
    void BruteForceBroadPhase::Clear()
    {
        // capacity は残す（毎フレーム同じくらいの件数を入れ直すため）
        proxies_.clear();
        moving_.clear();
    }

    uint32_t BruteForceBroadPhase::Insert(const Proxy& proxy)
    {
        const uint32_t index = static_cast<uint32_t>(proxies_.size());
        proxies_.push_back(proxy);
        if (!proxy.isStatic) {
            moving_.push_back(index);
        }
        return index;
    }

    void BruteForceBroadPhase::CollectPairs(std::vector<PairIndices>& outPairs)
    {
        const uint32_t count = static_cast<uint32_t>(proxies_.size());
        const auto test = [&](uint32_t i, uint32_t j) {
            if (!ShouldTestPair(proxies_[i], proxies_[j])) { return; }
            if (!Geometry::Intersect(proxies_[i].bounds, proxies_[j].bounds)) { return; }
            outPairs.emplace_back(i, j);
        };

        // ペアは i の昇順、同じ i の中では j の昇順に並べる
        for (uint32_t i = 0; i < count; ++i) {
            if (proxies_[i].isStatic) {
                // 相手は i より後ろの動くものだけ
                for (auto it = std::upper_bound(moving_.begin(), moving_.end(), i); it != moving_.end(); ++it) {
                    test(i, *it);
                }
            } else {
                for (uint32_t j = i + 1; j < count; ++j) {
                    test(i, j);
                }
            }
        }
    }
}
}
