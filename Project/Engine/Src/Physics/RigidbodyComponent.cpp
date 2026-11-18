#include "pch.h"
#include "RigidbodyComponent.h"

#include "Inertia.h"

#include "Collision/ColliderComponent.h"
#include "GameObject/GameObject.h"
#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "Math/MathCore.h"

REFLECT_REGISTER(CoreEngine::RigidbodyComponent)
COMPONENT_REGISTER(CoreEngine::RigidbodyComponent)

namespace CoreEngine
{
    bool RigidbodyComponent::RequiresComponent(const IComponent& other) const
    {
        return IsAnyOf<TransformComponent>(other);
    }

    void RigidbodyComponent::Start()
    {
        transform_ = Sibling<TransformComponent>();
        RefreshInertia();
    }

    void RigidbodyComponent::AddForce(const Vector3& force)
    {
        accumulatedForce_ += force;
        WakeUp();
    }

    void RigidbodyComponent::AddImpulse(const Vector3& impulse)
    {
        velocity_ += LockLinear(impulse * GetInverseMass());
        WakeUp();
    }

    void RigidbodyComponent::AddTorque(const Vector3& torque)
    {
        accumulatedTorque_ += torque;
        WakeUp();
    }

    void RigidbodyComponent::AddImpulseAtPoint(const Vector3& impulse, const Vector3& worldPoint)
    {
        velocity_ += LockLinear(impulse * GetInverseMass());

        if (!IsRotationFrozen() && GetOwner() != nullptr) {
            const Vector3 lever = worldPoint - GetOwner()->GetWorldPosition();
            angularVelocity_ += ApplyInverseInertia(Cross(lever, impulse));
        }
        WakeIfSleeping();
    }

    void RigidbodyComponent::SetVelocity(const Vector3& velocity)
    {
        velocity_ = velocity;
        WakeIfSleeping();
    }

    void RigidbodyComponent::SetAngularVelocity(const Vector3& angularVelocity)
    {
        angularVelocity_ = angularVelocity;
        WakeIfSleeping();
    }

    void RigidbodyComponent::SetBodyType(BodyType type)
    {
        bodyType_ = type;
        WakeIfSleeping();
    }

    void RigidbodyComponent::SetMass(float mass)
    {
        mass_ = (mass > kMinimumMass) ? mass : kMinimumMass;
    }

    float RigidbodyComponent::GetInverseMass() const
    {
        return IsDynamic() ? (1.0f / mass_) : 0.0f;
    }

    void RigidbodyComponent::SetRotationFrozen(bool frozen)
    {
        freezeRotationX_ = frozen;
        freezeRotationY_ = frozen;
        freezeRotationZ_ = frozen;
    }

    Vector3 RigidbodyComponent::LockLinear(const Vector3& worldVector) const
    {
        return {
            freezePositionX_ ? 0.0f : worldVector.x,
            freezePositionY_ ? 0.0f : worldVector.y,
            freezePositionZ_ ? 0.0f : worldVector.z
        };
    }

    Vector3 RigidbodyComponent::LockAngular(const Vector3& worldVector) const
    {
        return {
            freezeRotationX_ ? 0.0f : worldVector.x,
            freezeRotationY_ ? 0.0f : worldVector.y,
            freezeRotationZ_ ? 0.0f : worldVector.z
        };
    }

    float RigidbodyComponent::GetInverseMassAlong(const Vector3& axis) const
    {
        // 止めていない向きの成分の長さの 2 乗だけ効く
        const Vector3 free = LockLinear(axis);
        return GetInverseMass() * Dot(free, free);
    }

    void RigidbodyComponent::UpgradeSavedParameters(uint32_t fromVersion, uint32_t toVersion, json& parameters)
    {
        (void)fromVersion;
        if (toVersion != 2) {
            return;
        }
        const auto it = parameters.find("freezeRotation");
        if (it == parameters.end()) {
            return;
        }
        const bool frozen = it->is_boolean() && it->get<bool>();
        parameters.erase(it);
        parameters["freezeRotationX"] = frozen;
        parameters["freezeRotationY"] = frozen;
        parameters["freezeRotationZ"] = frozen;
    }

    void RigidbodyComponent::IntegrateVelocity(PhysicsKey, const Vector3& gravity, float deltaTime)
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
        velocity_ = LockLinear(velocity_);
    }

    void RigidbodyComponent::IntegrateAngularVelocity(PhysicsKey, float deltaTime)
    {
        if (!IsDynamic() || IsRotationFrozen()) {
            accumulatedTorque_ = {};
            angularVelocity_ = IsRotationFrozen() ? Vector3{} : angularVelocity_;
            return;
        }

        angularVelocity_ += ApplyInverseInertia(accumulatedTorque_) * deltaTime;
        accumulatedTorque_ = {};

        // 減り方がステップ幅に依らないよう、割り算で掛ける
        if (angularDamping_ > 0.0f) {
            angularVelocity_ *= 1.0f / (1.0f + angularDamping_ * deltaTime);
        }
        angularVelocity_ = LockAngular(angularVelocity_);
    }

    void RigidbodyComponent::IntegrateRotation(PhysicsKey, float deltaTime)
    {
        if (bodyType_ == BodyType::Static || IsRotationFrozen()) {
            return;
        }
        if (LengthSquared(angularVelocity_) <= 0.0f) {
            return;
        }

        TransformComponent* const transform = FindTransform();
        if (!transform) {
            return;
        }

        // dq/dt = 0.5 * ω * q（ω は実部 0 のクォータニオン）
        const Quaternion spin{ angularVelocity_.x, angularVelocity_.y, angularVelocity_.z, 0.0f };
        const Quaternion current = transform->GetRotation();
        const Quaternion delta = (spin * current) * (0.5f * deltaTime);

        transform->SetRotation(MathCore::QuaternionMath::Normalize(current + delta));
    }

    void RigidbodyComponent::IntegratePosition(PhysicsKey key, float deltaTime)
    {
        if (bodyType_ == BodyType::Static) {
            return;
        }
        ApplyPositionDelta(key, velocity_ * deltaTime);
    }

    void RigidbodyComponent::ApplyPositionDelta(PhysicsKey, const Vector3& delta)
    {
        if (TransformComponent* const transform = FindTransform()) {
            transform->ApplyWorldDelta(LockLinear(delta));
        }
    }

    void RigidbodyComponent::SetSleeping(bool sleeping)
    {
        if (!sleeping) {
            WakeUp();
            return;
        }
        sleeping_ = true;
        velocity_ = {};
        angularVelocity_ = {};
    }

    void RigidbodyComponent::WakeUp()
    {
        sleeping_ = false;
        stillTime_ = 0.0f;
    }

    void RigidbodyComponent::WakeIfSleeping()
    {
        if (sleeping_) {
            WakeUp();
        }
    }

    void RigidbodyComponent::UpdateSleepState(PhysicsKey, float deltaTime, float linearThreshold,
                                              float angularThreshold, float timeToSleep)
    {
        if (!IsDynamic() || sleeping_) {
            return;
        }

        const bool still = LengthSquared(velocity_) <= linearThreshold * linearThreshold
            && LengthSquared(angularVelocity_) <= angularThreshold * angularThreshold;

        if (!still) {
            stillTime_ = 0.0f;
            return;
        }

        stillTime_ += deltaTime;
        if (stillTime_ >= timeToSleep) {
            SetSleeping(true);
        }
    }

    Vector3 RigidbodyComponent::GetVelocityAtPoint(const Vector3& worldPoint) const
    {
        if (GetOwner() == nullptr) {
            return velocity_;
        }
        const Vector3 lever = worldPoint - GetOwner()->GetWorldPosition();
        return velocity_ + Cross(angularVelocity_, lever);
    }

    Vector3 RigidbodyComponent::ApplyInverseInertia(const Vector3& worldVector) const
    {
        if (IsRotationFrozen() || GetOwner() == nullptr) {
            return {};
        }

        // 止めた軸を外す → ローカル軸 → 逆慣性を掛ける → ワールドへ戻す → 止めた軸を外す
        Vector3 axes[3];
        GetOwner()->GetWorldAxes(axes[0], axes[1], axes[2]);

        const float inverse[3] = {
            localInverseInertia_.x, localInverseInertia_.y, localInverseInertia_.z
        };

        const Vector3 free = LockAngular(worldVector);
        Vector3 result{};
        for (int axis = 0; axis < 3; ++axis) {
            result += axes[axis] * (Dot(free, axes[axis]) * inverse[axis]);
        }
        return LockAngular(result);
    }

    float RigidbodyComponent::GetMinimumExtent() const
    {
        const Vector3 scale = GetOwner() ? GetOwner()->GetWorldScale() : Vector3{ 1.0f, 1.0f, 1.0f };
        const Collider* const collider = GetOwner() ? GetColliderShape() : nullptr;

        if (!collider) {
            return 0.5f;
        }

        // メッシュは三角形を包む箱として見る
        if (collider->GetShapeType() == ColliderShapeType::Box
            || collider->GetShapeType() == ColliderShapeType::Mesh) {
            const Vector3 size = collider->GetLocalBoxSize();
            return (std::min)({ size.x * std::abs(scale.x),
                                size.y * std::abs(scale.y),
                                size.z * std::abs(scale.z) }) * 0.5f;
        }

        if (collider->GetShapeType() == ColliderShapeType::Capsule) {
            // いちばん薄いのは横から見たときの太さ
            return WorldCapsuleRadius(collider->GetShape(), scale);
        }

        return collider->GetShape().radius * MaxAbsScale(scale);
    }

    void RigidbodyComponent::RefreshInertia()
    {
        if (!IsDynamic()) {
            localInverseInertia_ = {};
            return;
        }

        const Vector3 scale = GetOwner() ? GetOwner()->GetWorldScale() : Vector3{ 1.0f, 1.0f, 1.0f };
        const Collider* const collider =
            GetOwner() ? GetColliderShape() : nullptr;

        if (collider && (collider->GetShapeType() == ColliderShapeType::Box
                      || collider->GetShapeType() == ColliderShapeType::Mesh)) {
            const Vector3 size = collider->GetLocalBoxSize();
            const Vector3 scaledSize{
                size.x * std::abs(scale.x),
                size.y * std::abs(scale.y),
                size.z * std::abs(scale.z)
            };
            localInverseInertia_ = Inertia::Invert(Inertia::ForBox(mass_, scaledSize));
            return;
        }

        if (collider && collider->GetShapeType() == ColliderShapeType::Capsule) {
            const CollisionShape& shape = collider->GetShape();
            localInverseInertia_ = Inertia::Invert(Inertia::ForCapsule(
                mass_, WorldCapsuleRadius(shape, scale), shape.CylinderLength() * std::abs(scale.y)));
            return;
        }

        // 球、またはコライダーが無いとき
        const float maxScale = MaxAbsScale(scale);
        const float radius = collider ? (collider->GetShape().radius * maxScale) : (0.5f * maxScale);
        localInverseInertia_ = Inertia::Invert(Inertia::ForSphere(mass_, radius));
    }

    const Collider* RigidbodyComponent::GetColliderShape() const
    {
        const auto* const colliders = Sibling<ColliderComponent>();
        if (!colliders) {
            return nullptr;
        }
        for (size_t index = 0; index < colliders->Count(); ++index) {
            const Collider* const collider = colliders->Get(index);
            if (collider && (!IsDynamic() || collider->GetShapeType() != ColliderShapeType::Mesh)) {
                return collider;
            }
        }
        return nullptr;
    }

    TransformComponent* RigidbodyComponent::FindTransform()
    {
        return CachedSibling(transform_);
    }

    bool IsShapeUsedByPhysics(const Collider& collider)
    {
        if (collider.GetShapeType() != ColliderShapeType::Mesh) {
            return true;
        }
        GameObject* const owner = collider.GetOwner();
        const RigidbodyComponent* const body = owner ? owner->GetComponent<RigidbodyComponent>() : nullptr;
        return !(body && body->IsEnabled() && body->IsDynamic());
    }
}
