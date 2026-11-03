#include "pch.h"
#include "BuoyancyComponent.h"

#include <algorithm>
#include <cmath>

#include "Collision/ColliderComponent.h"
#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/GameObject.h"
#include "Math/MathCore.h"
#include "Physics/RigidbodyComponent.h"

REFLECT_REGISTER(CoreEngine::BuoyancyComponent)
COMPONENT_REGISTER(CoreEngine::BuoyancyComponent)

namespace CoreEngine
{
    bool BuoyancyComponent::RequiresComponent(const IComponent& other) const
    {
        return dynamic_cast<const RigidbodyComponent*>(&other) != nullptr;
    }

    void BuoyancyComponent::OnEnable()
    {
        points_.fill(WaterQuery::kInvalidPoint);
        submergedRatio_ = 0.0f;
        query_ = FindWaterQuery(GetOwner());
        if (!query_ || !GetOwner()) {
            return;
        }
        const Vector3 position = GetOwner()->GetWorldPosition();
        for (uint32_t& point : points_) {
            point = query_->AddPoint(position);
        }
    }

    void BuoyancyComponent::OnDisable()
    {
        if (query_) {
            for (const uint32_t point : points_) {
                query_->RemovePoint(point);
            }
        }
        query_ = nullptr;
        points_.fill(WaterQuery::kInvalidPoint);
        submergedRatio_ = 0.0f;
    }

    float BuoyancyComponent::BuildCells(std::array<Cell, kSampleCount>& cells) const
    {
        using MathCore::Constants::kPi;

        const GameObject* const owner = GetOwner();
        const Vector3 scale = owner ? owner->GetWorldScale() : Vector3{ 1.0f, 1.0f, 1.0f };
        const Vector3 absScale{ std::abs(scale.x), std::abs(scale.y), std::abs(scale.z) };
        const float maxScale = (std::max)({ absScale.x, absScale.y, absScale.z });
        const auto* const colliders = Sibling<ColliderComponent>();
        const Collider* const collider = colliders ? colliders->GetFirst() : nullptr;

        // 形を囲む箱（オーナーの軸に沿った 3 辺）と、形全体の体積
        Vector3 extent{};
        float volume = 0.0f;
        if (collider && collider->GetShapeType() == ColliderShapeType::Box) {
            const Vector3& size = collider->GetShape().size;
            extent = { size.x * absScale.x, size.y * absScale.y, size.z * absScale.z };
            volume = extent.x * extent.y * extent.z;
        } else if (collider && collider->GetShapeType() == ColliderShapeType::Capsule) {
            const CollisionShape& shape = collider->GetShape();
            const float radius = shape.radius * (std::max)(absScale.x, absScale.z);
            const float height = (std::max)(shape.height * absScale.y, 2.0f * radius);
            extent = { 2.0f * radius, height, 2.0f * radius };
            volume = kPi * radius * radius * (height - 2.0f * radius) + (4.0f / 3.0f) * kPi * radius * radius * radius;
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
        for (uint32_t i = 0; i < kSampleCount; ++i) {
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

    void BuoyancyComponent::ApplyForces(PhysicsKey, float deltaTime, const Vector3& gravity)
    {
        submergedRatio_ = 0.0f;
        GameObject* const owner = GetOwner();
        RigidbodyComponent* const body = Sibling<RigidbodyComponent>();
        if (!query_ || !owner || !body || !body->IsDynamic() || deltaTime <= 0.0f) {
            return;
        }

        std::array<Cell, kSampleCount> cells{};
        const float shapeVolume = BuildCells(cells);
        const float totalVolume = volume_ > 0.0f ? volume_ : shapeVolume;
        if (totalVolume <= 0.0f) {
            return;
        }
        const float cellVolume = totalVolume / static_cast<float>(kSampleCount);
        const float cellMass = body->GetMass() / static_cast<float>(kSampleCount);

        Vector3 axisX;
        Vector3 axisY;
        Vector3 axisZ;
        owner->GetWorldAxes(axisX, axisY, axisZ);
        const Vector3 position = owner->GetWorldPosition();

        // 浮力は重力の逆向き。重力を受けない剛体には浮力を加えない（抵抗だけ）
        const float gravityLength = Length(gravity);
        const bool buoyant = body->UsesGravity() && gravityLength > 0.0f;
        const Vector3 up = gravityLength > 0.0f ? gravity * (-1.0f / gravityLength) : Vector3{ 0.0f, 1.0f, 0.0f };

        float submergedSum = 0.0f;
        for (uint32_t i = 0; i < kSampleCount; ++i) {
            const Cell& cell = cells[i];
            const Vector3 lever = axisX * cell.localCenter.x + axisY * cell.localCenter.y + axisZ * cell.localCenter.z;
            const Vector3 point = position + lever;
            query_->SetPosition(points_[i], point);
            const WaterSample sample = query_->Sample(points_[i], point);
            if (!sample.inWater) {
                continue;
            }

            // 回った塊を囲む箱の上下の幅のうち、水面より下にある割合
            const float cellHeight = (std::max)(
                std::abs(axisX.y) * cell.size.x + std::abs(axisY.y) * cell.size.y + std::abs(axisZ.y) * cell.size.z,
                1.0e-4f);
            const float bottom = point.y - 0.5f * cellHeight;
            const float submerged = std::clamp((sample.surfaceHeight - bottom) / cellHeight, 0.0f, 1.0f);
            if (submerged <= 0.0f) {
                continue;
            }
            submergedSum += submerged;

            Vector3 force{};
            if (buoyant) {
                force = up * (waterDensity_ * gravityLength * cellVolume * submerged);
            }
            // 水の抵抗：速さに比例する分と、動く向きから見た塊の面積で速さの 2 乗に比例する分
            const Vector3 pointVelocity = body->GetVelocityAtPoint(point);
            const float speed = Length(pointVelocity);
            float dragRate = waterDrag_ * submerged;
            if (speed > 1.0e-4f) {
                const Vector3 direction = pointVelocity * (1.0f / speed);
                const float area = std::abs(Dot(direction, axisX)) * cell.size.y * cell.size.z
                    + std::abs(Dot(direction, axisY)) * cell.size.x * cell.size.z
                    + std::abs(Dot(direction, axisZ)) * cell.size.x * cell.size.y;
                dragRate += 0.5f * waterDensity_ * kDragCoefficient * area * speed * submerged / cellMass;
            }
            // 率は 1 ステップで速さが逆向きにならない大きさまでにする
            dragRate = (std::min)(dragRate, 1.0f / deltaTime);
            force = force - pointVelocity * (cellMass * dragRate);
            body->AddForce(force);
            body->AddTorque(Cross(lever, force));
        }

        submergedRatio_ = submergedSum / static_cast<float>(kSampleCount);
        if (submergedRatio_ > 0.0f && waterAngularDrag_ > 0.0f) {
            const float keep = (std::max)(0.0f, 1.0f - waterAngularDrag_ * submergedRatio_ * deltaTime);
            body->SetAngularVelocity(body->GetAngularVelocity() * keep);
        }
    }
}
