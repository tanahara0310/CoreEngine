#include "pch.h"
#include "RigidbodyComponent.h"

#include "GameObject/GameObject.h"
#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/Component/Transform/TransformComponent.h"

REFLECT_REGISTER(CoreEngine::RigidbodyComponent)
COMPONENT_REGISTER(CoreEngine::RigidbodyComponent)

namespace CoreEngine
{
    bool RigidbodyComponent::RequiresComponent(const IComponent& other) const
    {
        return dynamic_cast<const TransformComponent*>(&other) != nullptr;
    }

    void RigidbodyComponent::Start()
    {
        transform_ = Sibling<TransformComponent>();
    }

    void RigidbodyComponent::AddForce(const Vector3& force)
    {
        accumulatedForce_ += force;
    }

    void RigidbodyComponent::AddImpulse(const Vector3& impulse)
    {
        velocity_ += impulse * GetInverseMass();
    }

    void RigidbodyComponent::SetMass(float mass)
    {
        mass_ = (mass > kMinimumMass) ? mass : kMinimumMass;
    }

    float RigidbodyComponent::GetInverseMass() const
    {
        return IsDynamic() ? (1.0f / mass_) : 0.0f;
    }

    void RigidbodyComponent::IntegrateVelocity(const Vector3& gravity, float deltaTime)
    {
        if (!IsDynamic()) {
            accumulatedForce_ = {};
            return;
        }

        if (useGravity_) {
            velocity_ += gravity * deltaTime;
        }
        velocity_ += accumulatedForce_ * (GetInverseMass() * deltaTime);
        accumulatedForce_ = {};

        // 減り方がステップ幅に依らないよう、割り算で掛ける
        if (linearDamping_ > 0.0f) {
            velocity_ *= 1.0f / (1.0f + linearDamping_ * deltaTime);
        }
    }

    void RigidbodyComponent::IntegratePosition(float deltaTime)
    {
        if (bodyType_ == BodyType::Static) {
            return;
        }
        ApplyPositionDelta(velocity_ * deltaTime);
    }

    void RigidbodyComponent::ApplyPositionDelta(const Vector3& delta)
    {
        if (TransformComponent* const transform = FindTransform()) {
            transform->ApplyWorldDelta(delta);
        }
    }

    TransformComponent* RigidbodyComponent::FindTransform()
    {
        if (!transform_) {
            transform_ = Sibling<TransformComponent>();
        }
        return transform_;
    }
}
