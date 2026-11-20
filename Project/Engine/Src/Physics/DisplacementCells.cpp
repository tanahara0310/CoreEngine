#include "pch.h"
#include "DisplacementCells.h"

#include <algorithm>
#include <cmath>

#include "Collision/Collider.h"
#include "GameObject/GameObject.h"
#include "Math/MathCore.h"

namespace CoreEngine
{
    float BuildDisplacementCells(
        const GameObject& owner, const Collider* collider, DisplacementFallbackShape fallback, DisplacementCells& cells)
    {
        using MathCore::Constants::kPi;

        const Vector3 scale = owner.GetWorldScale();
        const Vector3 absScale{ std::abs(scale.x), std::abs(scale.y), std::abs(scale.z) };
        const float maxScale = MaxAbsScale(scale);

        // 形を囲む箱（オーナーの軸に沿った 3 辺）と、形全体の体積
        Vector3 extent{};
        float volume = 0.0f;
        if (collider && (collider->GetShapeType() == ColliderShapeType::Box
                      || collider->GetShapeType() == ColliderShapeType::Mesh)) {
            const Vector3 size = collider->GetLocalBoxSize();
            extent = { size.x * absScale.x, size.y * absScale.y, size.z * absScale.z };
            volume = extent.x * extent.y * extent.z;
        } else if (collider && collider->GetShapeType() == ColliderShapeType::Capsule) {
            const CollisionShape& shape = collider->GetShape();
            const float radius = WorldCapsuleRadius(shape, scale);
            const float height = (std::max)(shape.height * absScale.y, 2.0f * radius);
            extent = { 2.0f * radius, height, 2.0f * radius };
            volume = kPi * radius * radius * (height - 2.0f * radius) + (4.0f / 3.0f) * kPi * radius * radius * radius;
        } else if (!collider && fallback == DisplacementFallbackShape::UnitBox) {
            extent = absScale;
            volume = extent.x * extent.y * extent.z;
        } else {
            const float radius = (collider ? collider->GetShape().radius : 0.5f) * maxScale;
            extent = { 2.0f * radius, 2.0f * radius, 2.0f * radius };
            volume = (4.0f / 3.0f) * kPi * radius * radius * radius;
        }

        Vector3 center{};
        if (collider) {
            const Vector3& offset = collider->GetShape().offset;
            center = { offset.x * scale.x, offset.y * scale.y, offset.z * scale.z };
        }

        // 箱を縦横高さ 2 つずつに分ける
        const Vector3 cellSize{ extent.x * 0.5f, extent.y * 0.5f, extent.z * 0.5f };
        for (uint32_t i = 0; i < kDisplacementCellCount; ++i) {
            const float signX = (i & 1u) ? 0.5f : -0.5f;
            const float signY = (i & 2u) ? 0.5f : -0.5f;
            const float signZ = (i & 4u) ? 0.5f : -0.5f;
            cells[i].localCenter = {
                center.x + signX * cellSize.x,
                center.y + signY * cellSize.y,
                center.z + signZ * cellSize.z,
            };
            cells[i].size = cellSize;
        }
        return volume;
    }

    void PlaceDisplacementCells(const GameObject& owner, const DisplacementCells& cells, PlacedDisplacementCells& out)
    {
        Vector3 axisX;
        Vector3 axisY;
        Vector3 axisZ;
        owner.GetWorldAxes(axisX, axisY, axisZ);
        const Vector3 position = owner.GetWorldPosition();
        const float upX = std::abs(axisX.y);
        const float upY = std::abs(axisY.y);
        const float upZ = std::abs(axisZ.y);

        for (uint32_t i = 0; i < kDisplacementCellCount; ++i) {
            const DisplacementCell& cell = cells[i];
            PlacedDisplacementCell& placed = out[i];
            placed.lever = axisX * cell.localCenter.x + axisY * cell.localCenter.y + axisZ * cell.localCenter.z;
            placed.center = position + placed.lever;
            placed.height = (std::max)(upX * cell.size.x + upY * cell.size.y + upZ * cell.size.z, 1.0e-4f);
            placed.area = upX * cell.size.y * cell.size.z + upY * cell.size.x * cell.size.z + upZ * cell.size.x * cell.size.y;
        }
    }
}
