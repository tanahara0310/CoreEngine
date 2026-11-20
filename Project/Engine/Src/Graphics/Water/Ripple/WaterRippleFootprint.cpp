#include "pch.h"
#include "WaterRippleFootprint.h"

#include <algorithm>
#include <cmath>

#include "Graphics/Water/Ripple/WaterRippleSimulation.h"
#include "Math/MathCore.h"

namespace CoreEngine
{
    namespace
    {
        /// 柱の下の塊の番号（上の塊は Y のビットを立てた番号）
        constexpr uint32_t kColumnLowerCells[WaterRippleFootprint::kColumnCount] = { 0u, 1u, 4u, 5u };
        constexpr uint32_t kUpperCellBit = 2u;
        /// これより小さい変化は覚えている形に溜めて、次に動いたときにまとめて入れる
        constexpr float kStillMeters = 1.0e-4f;
        constexpr float kStillDepth = 1.0e-5f;

        float CellBottom(const PlacedDisplacementCell& cell) { return cell.center.y - 0.5f * cell.height; }
        float CellTop(const PlacedDisplacementCell& cell) { return cell.center.y + 0.5f * cell.height; }

        /// @brief 塊の真上・真下の、波紋を除いた水面の高さ [m]
        float BaseSurfaceHeight(const WaterRippleFootprint::CellState& cell)
        {
            return cell.water.surfaceHeight - cell.water.rippleHeight;
        }

        /// @brief 塊のうち、波紋を除いた水面より下にある高さ [m]
        float SubmergedHeight(const WaterRippleFootprint::CellState& cell)
        {
            return std::clamp(BaseSurfaceHeight(cell) - CellBottom(cell.placed), 0.0f, cell.placed.height);
        }
    }

    void WaterRippleFootprint::Emit(
        WaterRippleSimulation& simulation, std::span<const CellState, kDisplacementCellCount> cells, float strength)
    {
        using MathCore::Constants::kPi;

        if (!(strength > 0.0f) || !std::isfinite(strength) || !simulation.IsInitialized()) {
            Reset();
            return;
        }

        std::array<Column, kColumnCount> current{};
        float volume = 0.0f;
        for (uint32_t c = 0; c < kColumnCount; ++c) {
            const CellState& lower = cells[kColumnLowerCells[c]];
            const CellState& upper = cells[kColumnLowerCells[c] | kUpperCellBit];
            if (!lower.water.inWater || !upper.water.inWater) {
                Reset();
                return;
            }

            // 柱の上端が水面から柱の高さ以上深いと水面を押さない（その間は浅いほど強く押す）
            const float top = (std::max)(CellTop(lower.placed), CellTop(upper.placed));
            const float bottom = (std::min)(CellBottom(lower.placed), CellBottom(upper.placed));
            const float columnHeight = (std::max)(top - bottom, 1.0e-4f);
            const float surface = (std::max)(BaseSurfaceHeight(lower), BaseSurfaceHeight(upper));
            const float reach = std::clamp(1.0f - (surface - top) / columnHeight, 0.0f, 1.0f);
            const float depth = (SubmergedHeight(lower) + SubmergedHeight(upper)) * reach;
            const float area = 0.5f * (lower.placed.area + upper.placed.area);
            volume += depth * area;

            // 柱の広さと同じ体積のガウスの盛り上がり（2πσ² = 広さ）
            Column& column = current[c];
            column.x = 0.5f * (lower.placed.center.x + upper.placed.center.x);
            column.y = 0.5f * (lower.placed.center.y + upper.placed.center.y);
            column.z = 0.5f * (lower.placed.center.z + upper.placed.center.z);
            column.radius = 2.0f * std::sqrt(area / (2.0f * kPi));
            column.depth = depth * strength;
        }
        displacedVolume_ = volume;

        if (!hasPrevious_) {
            previous_ = current;
            hasPrevious_ = true;
            return;
        }

        bool still = true;
        uint32_t sourceCount = 0;
        for (uint32_t c = 0; c < kColumnCount; ++c) {
            const Column& before = previous_[c];
            const Column& now = current[c];
            const float distance = std::hypot(now.x - before.x, now.z - before.z);
            if (std::hypot(distance, now.y - before.y) > kTeleportMeters) {
                previous_ = current;
                return;
            }
            if (distance > kStillMeters || std::abs(now.radius - before.radius) > kStillMeters
                || std::abs(now.depth - before.depth) > kStillDepth) {
                still = false;
            }
            sourceCount += (before.depth != 0.0f ? 1u : 0u) + (now.depth != 0.0f ? 1u : 0u);
        }
        if (still || simulation.GetFreeSourceCount() < sourceCount) {
            return;
        }

        // 前の盛り上がりを引いて今の盛り上がりを足す（AddRipple の深さはマイナスで盛り上げる）
        for (uint32_t c = 0; c < kColumnCount; ++c) {
            const Column& before = previous_[c];
            const Column& now = current[c];
            if (before.depth != 0.0f) {
                simulation.AddObjectRipple(Vector3{ before.x, 0.0f, before.z }, before.radius, before.depth);
            }
            if (now.depth != 0.0f) {
                simulation.AddObjectRipple(Vector3{ now.x, 0.0f, now.z }, now.radius, -now.depth);
            }
        }
        previous_ = current;
    }
}
