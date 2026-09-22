#include "pch.h"
#include "ContactSolver.h"
#include "PhysicsMaterialComponent.h"
#include "RigidbodyComponent.h"

#include "Collision/Collider.h"
#include "Collision/CollisionWorld.h"
#include "GameObject/GameObject.h"

#include <cmath>
#include <limits>

namespace CoreEngine
{
    namespace {
        /// @brief コライダーの持ち主から剛体を引く（無い・無効なら nullptr）
        RigidbodyComponent* FindBody(const Collider* collider)
        {
            GameObject* const owner = collider ? collider->GetOwner() : nullptr;
            if (!owner) {
                return nullptr;
            }
            RigidbodyComponent* const body = owner->GetComponent<RigidbodyComponent>();
            return (body && body->IsEnabled()) ? body : nullptr;
        }

        /// @brief コライダーの持ち主から材質を引く（無い・無効なら nullptr）
        const PhysicsMaterialComponent* FindMaterial(const Collider* collider)
        {
            GameObject* const owner = collider ? collider->GetOwner() : nullptr;
            if (!owner) {
                return nullptr;
            }
            const auto* const material = owner->GetComponent<PhysicsMaterialComponent>();
            return (material && material->IsEnabled()) ? material : nullptr;
        }

        /// @brief 法線に直交する軸を 2 本作る
        void MakeTangents(const Vector3& normal, Vector3& tangent1, Vector3& tangent2)
        {
            // 法線と平行になりにくい方を基準に選んでから直交化する
            const Vector3 reference = (std::abs(normal.x) < 0.57735f)
                ? Vector3{ 1.0f, 0.0f, 0.0f }
                : Vector3{ 0.0f, 1.0f, 0.0f };

            tangent1 = Normalize(Cross(normal, reference));
            tangent2 = Cross(normal, tangent1);
        }
    }

    void ContactSolver::SetIterations(int count)
    {
        iterations_ = (count > 1) ? count : 1;
    }

    void ContactSolver::SetRestitutionThreshold(float speed)
    {
        restitutionThreshold_ = (speed > 0.0f) ? speed : 0.0f;
    }

    void ContactSolver::Build(const std::vector<ContactPair>& pairs)
    {
        constraints_.clear();

        for (const ContactPair& pair : pairs) {
            if (pair.a->IsTrigger() || pair.b->IsTrigger()) {
                continue;
            }

            ContactConstraint constraint;
            constraint.bodyA = FindBody(pair.a);
            constraint.bodyB = FindBody(pair.b);
            if (!constraint.bodyA && !constraint.bodyB) {
                continue;   // 物理が扱わない接触
            }

            constraint.inverseMassA = constraint.bodyA ? constraint.bodyA->GetInverseMass() : 0.0f;
            constraint.inverseMassB = constraint.bodyB ? constraint.bodyB->GetInverseMass() : 0.0f;
            constraint.inverseMassSum = constraint.inverseMassA + constraint.inverseMassB;
            if (constraint.inverseMassSum <= 0.0f) {
                continue;   // 両方とも無限質量
            }

            constraint.normal = pair.contact.normal;
            constraint.depth = pair.contact.depth;
            MakeTangents(constraint.normal, constraint.tangent1, constraint.tangent2);

            const PhysicsMaterialComponent* const materialA = FindMaterial(pair.a);
            const PhysicsMaterialComponent* const materialB = FindMaterial(pair.b);
            constraint.friction =
                PhysicsMaterialComponent::CombineFriction(materialA, materialB);

            // 跳ね返る速さは、解き始める前の近づく速さから決める
            const Vector3 velocityA =
                constraint.bodyA ? constraint.bodyA->GetVelocity() : Vector3{};
            const Vector3 velocityB =
                constraint.bodyB ? constraint.bodyB->GetVelocity() : Vector3{};
            const float approachSpeed = Dot(velocityA - velocityB, constraint.normal);

            if (approachSpeed > restitutionThreshold_) {
                const float restitution =
                    PhysicsMaterialComponent::CombineRestitution(materialA, materialB);
                constraint.targetSeparation = restitution * approachSpeed;
            }

            constraints_.push_back(constraint);
        }
    }

    void ContactSolver::SolveVelocities()
    {
        constexpr float kUnbounded = (std::numeric_limits<float>::max)();

        for (int iteration = 0; iteration < iterations_; ++iteration) {
            for (ContactConstraint& constraint : constraints_) {
                // 法線。押すだけで引っ張らないので下限は 0
                ApplyAxisImpulse(constraint, constraint.normal, constraint.normalImpulse,
                    0.0f, kUnbounded, constraint.targetSeparation);

                // 摩擦。クーロンの条件で法線インパルスに比例した範囲へ収める
                const float limit = constraint.friction * constraint.normalImpulse;
                ApplyAxisImpulse(constraint, constraint.tangent1, constraint.tangentImpulse1,
                    -limit, limit, 0.0f);
                ApplyAxisImpulse(constraint, constraint.tangent2, constraint.tangentImpulse2,
                    -limit, limit, 0.0f);
            }
        }
    }

    void ContactSolver::SolvePositions(float correctionRate, float slop)
    {
        for (ContactConstraint& constraint : constraints_) {
            const float excess = constraint.depth - slop;
            if (excess <= 0.0f) {
                continue;   // 許容の範囲なので触らない
            }

            // 軽い方が多く動くよう、質量の逆数の比で割り振る
            const float correction = excess * correctionRate / constraint.inverseMassSum;

            if (constraint.bodyA) {
                constraint.bodyA->ApplyPositionDelta(
                    constraint.normal * (-correction * constraint.inverseMassA));
            }
            if (constraint.bodyB) {
                constraint.bodyB->ApplyPositionDelta(
                    constraint.normal * (correction * constraint.inverseMassB));
            }
        }
    }

    void ContactSolver::ApplyAxisImpulse(ContactConstraint& constraint, const Vector3& axis,
                                         float& accumulated, float minImpulse, float maxImpulse,
                                         float bias)
    {
        const Vector3 velocityA = constraint.bodyA ? constraint.bodyA->GetVelocity() : Vector3{};
        const Vector3 velocityB = constraint.bodyB ? constraint.bodyB->GetVelocity() : Vector3{};

        // axis は a から b へ向かう向き。正で近づく
        const float approachSpeed = Dot(velocityA - velocityB, axis);

        float impulse = (approachSpeed + bias) / constraint.inverseMassSum;

        // 累積を範囲へ収めてから、その差分だけ与える
        const float previous = accumulated;
        accumulated = previous + impulse;
        if (accumulated < minImpulse) { accumulated = minImpulse; }
        if (accumulated > maxImpulse) { accumulated = maxImpulse; }
        impulse = accumulated - previous;

        if (impulse == 0.0f) {
            return;
        }

        if (constraint.bodyA) {
            constraint.bodyA->SetVelocity(velocityA - axis * (impulse * constraint.inverseMassA));
        }
        if (constraint.bodyB) {
            constraint.bodyB->SetVelocity(velocityB + axis * (impulse * constraint.inverseMassB));
        }
    }
}
