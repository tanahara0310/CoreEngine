#include "pch.h"
#include "UniformGridBroadPhase.h"
#include "Math/Geometry/Intersect.h"

#include <algorithm>
#include <cmath>

namespace CoreEngine
{
namespace BroadPhase
{
    namespace {
        /// @brief 64bit の混ぜ込み（splitmix64 の finalizer）
        /// @details セル座標をそのまま XOR すると格子状に偏るため、必ず混ぜる。
        inline uint64_t Mix64(uint64_t x)
        {
            x += 0x9E3779B97F4A7C15ull;
            x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
            x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
            return x ^ (x >> 31);
        }
    }

    size_t UniformGridBroadPhase::CellHash::operator()(const CellCoord& c) const noexcept
    {
        const uint64_t h = Mix64(static_cast<uint64_t>(static_cast<uint32_t>(c.x)))
                         ^ Mix64(static_cast<uint64_t>(static_cast<uint32_t>(c.y)) * 3ull)
                         ^ Mix64(static_cast<uint64_t>(static_cast<uint32_t>(c.z)) * 7ull);
        return static_cast<size_t>(h);
    }

    UniformGridBroadPhase::UniformGridBroadPhase(float cellSize)
    {
        SetCellSize(cellSize);
    }

    void UniformGridBroadPhase::SetCellSize(float cellSize)
    {
        cellSize_ = (cellSize > 1e-4f) ? cellSize : 1e-4f;
        invCellSize_ = 1.0f / cellSize_;
    }

    UniformGridBroadPhase::CellCoord UniformGridBroadPhase::ToCell(float x, float y, float z) const
    {
        return CellCoord{
            static_cast<int>(std::floor(x * invCellSize_)),
            static_cast<int>(std::floor(y * invCellSize_)),
            static_cast<int>(std::floor(z * invCellSize_)),
        };
    }

    void UniformGridBroadPhase::Clear()
    {
        proxies_.clear();
        oversized_.clear();
        // バケットは残して再利用する（毎フレーム同じ空間を張り直すため）
        for (auto& entry : cells_) {
            entry.second.clear();
        }
    }

    uint32_t UniformGridBroadPhase::Insert(const Proxy& proxy)
    {
        const uint32_t index = static_cast<uint32_t>(proxies_.size());
        proxies_.push_back(proxy);

        if (!proxy.bounds.IsValid()) {
            // 無効な AABB は空間に置けない。安全側に倒して常に候補にする。
            oversized_.push_back(index);
            return index;
        }

        const CellCoord minCell = ToCell(proxy.bounds.min.x, proxy.bounds.min.y, proxy.bounds.min.z);
        const CellCoord maxCell = ToCell(proxy.bounds.max.x, proxy.bounds.max.y, proxy.bounds.max.z);

        const uint64_t spanX = static_cast<uint64_t>(maxCell.x - minCell.x) + 1;
        const uint64_t spanY = static_cast<uint64_t>(maxCell.y - minCell.y) + 1;
        const uint64_t spanZ = static_cast<uint64_t>(maxCell.z - minCell.z) + 1;

        if (spanX * spanY * spanZ > kMaxCellsPerProxy) {
            // 巨大すぎる AABB はセル展開せず「常に候補」扱い
            oversized_.push_back(index);
            return index;
        }

        for (int z = minCell.z; z <= maxCell.z; ++z) {
            for (int y = minCell.y; y <= maxCell.y; ++y) {
                for (int x = minCell.x; x <= maxCell.x; ++x) {
                    cells_[CellCoord{ x, y, z }].push_back(index);
                }
            }
        }
        return index;
    }

    void UniformGridBroadPhase::CollectPairs(std::vector<PairIndices>& outPairs)
    {
        const size_t startSize = outPairs.size();

        // ── セル内のペア ───────────────────────────────────────────
        for (const auto& entry : cells_) {
            const auto& bucket = entry.second;
            for (size_t i = 0; i < bucket.size(); ++i) {
                for (size_t j = i + 1; j < bucket.size(); ++j) {
                    const uint32_t a = bucket[i];
                    const uint32_t b = bucket[j];
                    if (!ShouldTestPair(proxies_[a], proxies_[b])) { continue; }
                    if (!Geometry::Intersect(proxies_[a].bounds, proxies_[b].bounds)) { continue; }

                    outPairs.emplace_back((std::min)(a, b), (std::max)(a, b));
                }
            }
        }

        // ── 巨大 proxy は全件と突き合わせる ────────────────────────
        for (uint32_t a : oversized_) {
            for (uint32_t b = 0; b < proxies_.size(); ++b) {
                if (a == b) { continue; }
                if (!ShouldTestPair(proxies_[a], proxies_[b])) { continue; }
                if (!Geometry::Intersect(proxies_[a].bounds, proxies_[b].bounds)) { continue; }

                outPairs.emplace_back((std::min)(a, b), (std::max)(a, b));
            }
        }

        // ── 重複除去 ───────────────────────────────────────────────
        // 複数セルにまたがる物体は同じペアを何度も出す。
        auto begin = outPairs.begin() + static_cast<std::ptrdiff_t>(startSize);
        std::sort(begin, outPairs.end());
        outPairs.erase(std::unique(begin, outPairs.end()), outPairs.end());
    }
}
}
