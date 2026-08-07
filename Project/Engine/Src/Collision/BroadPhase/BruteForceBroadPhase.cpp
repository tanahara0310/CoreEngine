#include "pch.h"
#include "BruteForceBroadPhase.h"
#include "Math/Geometry/Intersect.h"

namespace CoreEngine
{
namespace BroadPhase
{
    void BruteForceBroadPhase::Clear()
    {
        // capacity は残す（毎フレーム同じくらいの件数を入れ直すため）
        proxies_.clear();
    }

    uint32_t BruteForceBroadPhase::Insert(const Proxy& proxy)
    {
        proxies_.push_back(proxy);
        return static_cast<uint32_t>(proxies_.size() - 1);
    }

    void BruteForceBroadPhase::CollectPairs(std::vector<PairIndices>& outPairs)
    {
        const uint32_t count = static_cast<uint32_t>(proxies_.size());

        for (uint32_t i = 0; i < count; ++i) {
            for (uint32_t j = i + 1; j < count; ++j) {
                if (!ShouldTestPair(proxies_[i], proxies_[j])) { continue; }
                if (!Geometry::Intersect(proxies_[i].bounds, proxies_[j].bounds)) { continue; }

                outPairs.emplace_back(i, j);
            }
        }
    }
}
}
