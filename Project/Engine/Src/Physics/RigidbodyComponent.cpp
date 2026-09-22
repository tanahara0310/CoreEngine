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
        return dynamic_cast<const TransformComponent*>(&other) != nullptr;
    }

    void RigidbodyComponent::Start()
    {
        transform_ = Sibling<TransformComponent>();

        // 角速度をオイラー角へ積むと軸が寝た瞬間に破綻するので、向きはクォータニオンで持つ
        if (transform_) {
            transform_->Get().EulerToQuaternion();
            transform_->Get().SetRotationMode(WorldTransform::RotationMode::Quaternion);
        }
        RefreshInertia();
    }

    void RigidbodyComponent::AddForce(const Vector3& force)
    {
        accumulatedForce_ += force;
        WakeUp();
    }

    void RigidbodyComponent::AddImpulse(const Vector3& impulse)
    {
        velocity_ += impulse * GetInverseMass();
        WakeUp();
    }

    void RigidbodyComponent::AddTorque(const Vector3& torque)
    {
        accumulatedTorque_ += torque;
        WakeUp();
    }

    void RigidbodyComponent::AddImpulseAtPoint(const Vector3& impulse, const Vector3& worldPoint)
    {
        velocity_ += impulse * GetInverseMass();

        if (!freezeRotation_ && GetOwner() != nullptr) {
            const Vector3 lever = worldPoint - GetOwner()->GetWorldPosition();
            angularVelocity_ += ApplyInverseInertia(Cross(lever, impulse));
        }
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

    void RigidbodyComponent::IntegrateAngularVelocity(float deltaTime)
    {
        if (!IsDynamic() || freezeRotation_) {
            accumulatedTorque_ = {};
            angularVelocity_ = freezeRotation_ ? Vector3{} : angularVelocity_;
            return;
        }

        angularVelocity_ += ApplyInverseInertia(accumulatedTorque_) * deltaTime;
        accumulatedTorque_ = {};

        // 減り方がステップ幅に依らないよう、割り算で掛ける
        if (angularDamping_ > 0.0f) {
            angularVelocity_ *= 1.0f / (1.0f + angularDamping_ * deltaTime);
        }
    }

    void RigidbodyComponent::IntegrateRotation(float deltaTime)
    {
        if (bodyType_ == BodyType::Static || freezeRotation_) {
            return;
        }
        if (LengthSquared(angularVelocity_) <= 0.0f) {
            return;
        }

        TransformComponent* const transform = FindTransform();
        if (!transform) {
            return;
        }

        WorldTransform& world = transform->Get();

        // dq/dt = 0.5 * ω * q（ω は実部 0 のクォータニオン）
        const Quaternion spin{ angularVelocity_.x, angularVelocity_.y, angularVelocity_.z, 0.0f };
        const Quaternion current = world.quaternionRotate;
        const Quaternion delta = (spin * current) * (0.5f * deltaTime);

        world.quaternionRotate = MathCore::QuaternionMath::Normalize(current + delta);

        // インスペクタ・ギズモ・保存はオイラー角を見るので、写しておく
        world.QuaternionToEuler();
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

    void RigidbodyComponent::UpdateSleepState(float deltaTime, float linearThreshold,
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
        if (freezeRotation_ || GetOwner() == nullptr) {
            return {};
        }

        // ワールド → ローカル軸 → 逆慣性を掛ける → ワールドへ戻す
        Vector3 axes[3];
        GetOwner()->GetWorldAxes(axes[0], axes[1], axes[2]);

        const float inverse[3] = {
            localInverseInertia_.x, localInverseInertia_.y, localInverseInertia_.z
        };

        Vector3 result{};
        for (int axis = 0; axis < 3; ++axis) {
            result += axes[axis] * (Dot(worldVector, axes[axis]) * inverse[axis]);
        }
        return result;
    }

    float RigidbodyComponent::GetMinimumExtent() const
    {
        const Vector3 scale = GetOwner() ? GetOwner()->GetWorldScale() : Vector3{ 1.0f, 1.0f, 1.0f };
        const Collider* const collider = GetOwner() ? GetColliderShape() : nullptr;

        if (!collider) {
            return 0.5f;
        }

        if (collider->GetShapeType() == ColliderShapeType::Box) {
            const Vector3& size = collider->GetShape().size;
            return (std::min)({ size.x * std::abs(scale.x),
                                size.y * std::abs(scale.y),
                                size.z * std::abs(scale.z) }) * 0.5f;
        }

        const float maxScale =
            (std::max)({ std::abs(scale.x), std::abs(scale.y), std::abs(scale.z) });
        return collider->GetShape().radius * maxScale;
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

        if (collider && collider->GetShapeType() == ColliderShapeType::Box) {
            const Vector3& size = collider->GetShape().size;
            const Vector3 scaledSize{
                size.x * std::abs(scale.x),
                size.y * std::abs(scale.y),
                size.z * std::abs(scale.z)
            };
            localInverseInertia_ = Inertia::Invert(Inertia::ForBox(mass_, scaledSize));
            return;
        }

        // 球、またはコライダーが無いとき
        const float maxScale =
            (std::max)({ std::abs(scale.x), std::abs(scale.y), std::abs(scale.z) });
        const float radius = collider ? (collider->GetShape().radius * maxScale) : (0.5f * maxScale);
        localInverseInertia_ = Inertia::Invert(Inertia::ForSphere(mass_, radius));
    }

    const Collider* RigidbodyComponent::GetColliderShape() const
    {
        const auto* const colliders = Sibling<ColliderComponent>();
        return colliders ? colliders->GetFirst() : nullptr;
    }

    TransformComponent* RigidbodyComponent::FindTransform()
    {
        if (!transform_) {
            transform_ = Sibling<TransformComponent>();
        }
        return transform_;
    }
}
