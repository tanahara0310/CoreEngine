#include "pch.h"
#include "WaterRippleEmitterComponent.h"

#include "Collision/ColliderComponent.h"
#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/GameObject.h"
#include "Graphics/Water/Ripple/WaterRippleSimulation.h"

REFLECT_REGISTER(CoreEngine::WaterRippleEmitterComponent)
COMPONENT_REGISTER(CoreEngine::WaterRippleEmitterComponent)

namespace CoreEngine
{
    void WaterRippleEmitterComponent::OnEnable()
    {
        points_.fill(WaterQuery::kInvalidPoint);
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

    void WaterRippleEmitterComponent::OnDisable()
    {
        if (query_) {
            for (const uint32_t point : points_) {
                query_->RemovePoint(point);
            }
        }
        query_ = nullptr;
        points_.fill(WaterQuery::kInvalidPoint);
        footprint_.Reset();
    }

    void WaterRippleEmitterComponent::LateUpdate()
    {
        GameObject* const owner = GetOwner();
        WaterRippleSimulation* const ripple = (query_ && owner && (strength_ > 0.0f || foamStrength_ > 0.0f))
            ? FindWaterRippleSimulation(owner->GetEngineSystem())
            : nullptr;
        if (!ripple) {
            footprint_.Reset();
            return;
        }

        const auto* const colliders = Sibling<ColliderComponent>();
        const Collider* const collider = colliders ? colliders->GetFirst() : nullptr;
        DisplacementCells cells{};
        BuildDisplacementCells(*owner, collider, DisplacementFallbackShape::UnitBox, cells);
        PlacedDisplacementCells placed{};
        PlaceDisplacementCells(*owner, cells, placed);

        std::array<WaterRippleFootprint::CellState, kDisplacementCellCount> states{};
        for (uint32_t i = 0; i < kDisplacementCellCount; ++i) {
            query_->SetPosition(points_[i], placed[i].center);
            states[i].placed = placed[i];
            states[i].water = query_->Sample(points_[i], placed[i].center);
        }
        footprint_.Emit(*ripple, states, strength_, foamStrength_);
    }
}
