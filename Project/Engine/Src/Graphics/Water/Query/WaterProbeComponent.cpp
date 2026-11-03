#include "pch.h"
#include "WaterProbeComponent.h"

#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/GameObject.h"

REFLECT_REGISTER(CoreEngine::WaterProbeComponent)
COMPONENT_REGISTER(CoreEngine::WaterProbeComponent)

namespace CoreEngine
{
    void WaterProbeComponent::OnEnable()
    {
        query_ = FindWaterQuery(GetOwner());
        if (query_ && GetOwner()) {
            point_ = query_->AddPoint(GetOwner()->GetWorldPosition());
        }
    }

    void WaterProbeComponent::OnDisable()
    {
        if (query_) {
            query_->RemovePoint(point_);
        }
        query_ = nullptr;
        point_ = WaterQuery::kInvalidPoint;
    }

    void WaterProbeComponent::LateUpdate()
    {
        if (query_ && GetOwner()) {
            query_->SetPosition(point_, GetOwner()->GetWorldPosition());
        }
    }

    float WaterProbeComponent::GetDepth() const
    {
        const WaterSample sample = GetSample();
        if (!sample.inWater || !GetOwner()) {
            return 0.0f;
        }
        return sample.surfaceHeight - GetOwner()->GetWorldPosition().y;
    }

    WaterSample WaterProbeComponent::GetSample() const
    {
        if (!query_ || !GetOwner()) {
            return {};
        }
        return query_->Sample(point_, GetOwner()->GetWorldPosition());
    }
}
