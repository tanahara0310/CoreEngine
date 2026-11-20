#include "pch.h"
#include "BuoyancyComponent.h"

#include <algorithm>
#include <cmath>

#include "Collision/ColliderComponent.h"
#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/GameObject.h"
#include "Graphics/Water/Ripple/WaterRippleSimulation.h"
#include "Physics/RigidbodyComponent.h"

REFLECT_REGISTER(CoreEngine::BuoyancyComponent)
COMPONENT_REGISTER(CoreEngine::BuoyancyComponent)

namespace CoreEngine
{
    bool BuoyancyComponent::RequiresComponent(const IComponent& other) const
    {
        return IsAnyOf<RigidbodyComponent>(other);
    }

    void BuoyancyComponent::OnEnable()
    {
        points_.fill(WaterQuery::kInvalidPoint);
        submergedRatio_ = 0.0f;
        footprint_.Reset();
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
        footprint_.Reset();
    }

    float BuoyancyComponent::BuildCells(DisplacementCells& cells) const
    {
        const GameObject* const owner = GetOwner();
        if (!owner) {
            return 0.0f;
        }
        const auto* const colliders = Sibling<ColliderComponent>();
        const Collider* const collider = colliders ? colliders->GetFirst() : nullptr;
        return BuildDisplacementCells(*owner, collider, DisplacementFallbackShape::Sphere, cells);
    }

    void BuoyancyComponent::LateUpdate()
    {
        GameObject* const owner = GetOwner();
        WaterRippleSimulation* const ripple = (query_ && owner && rippleStrength_ > 0.0f)
            ? FindWaterRippleSimulation(owner->GetEngineSystem())
            : nullptr;
        if (!ripple) {
            footprint_.Reset();
            return;
        }

        DisplacementCells cells{};
        BuildCells(cells);
        PlacedDisplacementCells placed{};
        PlaceDisplacementCells(*owner, cells, placed);
        std::array<WaterRippleFootprint::CellState, kSampleCount> states{};
        for (uint32_t i = 0; i < kSampleCount; ++i) {
            query_->SetPosition(points_[i], placed[i].center);
            states[i].placed = placed[i];
            states[i].water = query_->Sample(points_[i], placed[i].center);
        }
        footprint_.Emit(*ripple, states, rippleStrength_);
    }

    void BuoyancyComponent::ApplyForces(PhysicsKey, float deltaTime, const Vector3& gravity)
    {
        submergedRatio_ = 0.0f;
        GameObject* const owner = GetOwner();
        RigidbodyComponent* const body = Sibling<RigidbodyComponent>();
        if (!query_ || !owner || !body || !body->IsDynamic() || deltaTime <= 0.0f) {
            return;
        }

        DisplacementCells cells{};
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
        PlacedDisplacementCells placed{};
        PlaceDisplacementCells(*owner, cells, placed);

        // 浮力は重力の逆向き。重力を受けない剛体には浮力を加えない（抵抗だけ）
        const float gravityLength = Length(gravity);
        const bool buoyant = body->UsesGravity() && gravityLength > 0.0f;
        const Vector3 up = gravityLength > 0.0f ? gravity * (-1.0f / gravityLength) : Vector3{ 0.0f, 1.0f, 0.0f };

        // 波紋を起こす間は、物が起こした波紋を除いた水面に浮かべる
        const bool excludeObjectRipples = rippleStrength_ > 0.0f;

        float submergedSum = 0.0f;
        for (uint32_t i = 0; i < kSampleCount; ++i) {
            const DisplacementCell& cell = cells[i];
            const Vector3& lever = placed[i].lever;
            const Vector3& point = placed[i].center;
            query_->SetPosition(points_[i], point);
            const WaterSample sample = query_->Sample(points_[i], point);
            if (!sample.inWater) {
                continue;
            }
            const float surfaceHeight =
                excludeObjectRipples ? sample.surfaceHeight - sample.objectRippleHeight : sample.surfaceHeight;

            // 回った塊を囲む箱の上下の幅のうち、水面より下にある割合
            const float cellHeight = placed[i].height;
            const float bottom = point.y - 0.5f * cellHeight;
            const float submerged = std::clamp((surfaceHeight - bottom) / cellHeight, 0.0f, 1.0f);
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
