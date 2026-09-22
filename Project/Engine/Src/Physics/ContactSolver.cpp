#include "pch.h"
#include "ContactSolver.h"
#include "PhysicsMaterialComponent.h"
#include "RigidbodyComponent.h"

#include "Collision/Collider.h"
#include "Collision/CollisionWorld.h"
#include "Math/Geometry/Intersect.h"
#include "GameObject/GameObject.h"

#include <cmath>
#include <limits>

namespace CoreEngine
{
    namespace {
        /// 1 ペアから作る接触点の上限（箱の面どうしは 4 点で足りる）
        constexpr int kMaxManifoldPoints = 4;

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
        // 前のステップの結果を引き継ぐため、作り直す前に控える
        previous_.swap(constraints_);
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

            // どちらも動いていない接触は解かなくてよい。片方でも動いていれば両方起こす
            const bool restingA = !constraint.bodyA || constraint.bodyA->IsSleeping();
            const bool restingB = !constraint.bodyB || constraint.bodyB->IsSleeping();
            if (restingA && restingB) {
                continue;
            }
            // 起こすのは眠っている側だけ。起きている剛体へ呼ぶと、止まっている時間が
            // 毎ステップ 0 に戻って永久に眠れなくなる
            if (constraint.bodyA && constraint.bodyA->IsSleeping()) { constraint.bodyA->WakeUp(); }
            if (constraint.bodyB && constraint.bodyB->IsSleeping()) { constraint.bodyB->WakeUp(); }

            constraint.inverseMassA = constraint.bodyA ? constraint.bodyA->GetInverseMass() : 0.0f;
            constraint.inverseMassB = constraint.bodyB ? constraint.bodyB->GetInverseMass() : 0.0f;
            constraint.inverseMassSum = constraint.inverseMassA + constraint.inverseMassB;
            if (constraint.inverseMassSum <= 0.0f) {
                continue;   // 両方とも無限質量
            }

            constraint.colliderA = pair.a;
            constraint.colliderB = pair.b;
            constraint.normal = pair.contact.normal;
            MakeTangents(constraint.normal, constraint.tangent1, constraint.tangent2);

            const PhysicsMaterialComponent* const materialA = FindMaterial(pair.a);
            const PhysicsMaterialComponent* const materialB = FindMaterial(pair.b);
            constraint.friction =
                PhysicsMaterialComponent::CombineFriction(materialA, materialB);
            const float restitution =
                PhysicsMaterialComponent::CombineRestitution(materialA, materialB);

            // 面で触れている箱は接触点が複数要る。1 点だけだと支えが足りず倒れてしまう
            Geometry::Contact points[kMaxManifoldPoints];
            int pointCount = 0;

            if (pair.a->GetShapeType() == ColliderShapeType::Box
                && pair.b->GetShapeType() == ColliderShapeType::Box) {
                pointCount = Geometry::CollectBoxContacts(
                    pair.a->GetWorldOBB(), pair.b->GetWorldOBB(), constraint.normal,
                    points, kMaxManifoldPoints);
            }
            if (pointCount == 0) {
                points[0] = pair.contact;
                pointCount = 1;
            }

            for (int index = 0; index < pointCount; ++index) {
                ContactConstraint point = constraint;
                point.point = points[index].point;
                point.depth = points[index].depth;

                // 重心から接触点へのてこ。回転の寄与はこの腕で決まる
                if (point.bodyA && point.bodyA->GetOwner()) {
                    point.leverA = point.point - point.bodyA->GetOwner()->GetWorldPosition();
                }
                if (point.bodyB && point.bodyB->GetOwner()) {
                    point.leverB = point.point - point.bodyB->GetOwner()->GetWorldPosition();
                }

                // 跳ね返る速さは、解き始める前のその点での近づく速さから決める
                const Vector3 velocityA = point.bodyA
                    ? point.bodyA->GetVelocityAtPoint(point.point) : Vector3{};
                const Vector3 velocityB = point.bodyB
                    ? point.bodyB->GetVelocityAtPoint(point.point) : Vector3{};
                const float approachSpeed = Dot(velocityA - velocityB, point.normal);

                if (approachSpeed > restitutionThreshold_) {
                    point.targetSeparation = restitution * approachSpeed;
                }

                // 前のステップで同じ場所を解いていれば、その強さから始める
                if (const ContactConstraint* const last = FindPrevious(point)) {
                    point.normalImpulse = last->normalImpulse;
                    point.tangentImpulse1 = last->tangentImpulse1;
                    point.tangentImpulse2 = last->tangentImpulse2;
                }

                constraints_.push_back(point);
            }
        }
    }

    const ContactConstraint* ContactSolver::FindPrevious(const ContactConstraint& constraint) const
    {
        // 接触点は毎ステップ計算し直すので、同じ場所と見なす距離で探す
        constexpr float kSamePointDistanceSq = 0.0025f;   // 5cm

        for (const ContactConstraint& last : previous_) {
            if (last.bodyA != constraint.bodyA || last.bodyB != constraint.bodyB) {
                continue;
            }
            if (LengthSquared(last.point - constraint.point) <= kSamePointDistanceSq) {
                return &last;
            }
        }
        return nullptr;
    }

    void ContactSolver::WarmStart()
    {
        for (ContactConstraint& constraint : constraints_) {
            const Vector3 impulse = constraint.normal * constraint.normalImpulse
                                  + constraint.tangent1 * constraint.tangentImpulse1
                                  + constraint.tangent2 * constraint.tangentImpulse2;

            if (LengthSquared(impulse) <= 0.0f) {
                continue;
            }
            if (constraint.bodyA) {
                constraint.bodyA->AddImpulseAtPoint(impulse * -1.0f, constraint.point);
            }
            if (constraint.bodyB) {
                constraint.bodyB->AddImpulseAtPoint(impulse, constraint.point);
            }
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

    void ContactSolver::PublishImpulses()
    {
        for (const ContactConstraint& constraint : constraints_) {
            if (constraint.colliderA) {
                constraint.colliderA->AccumulateImpulse(constraint.normalImpulse);
            }
            if (constraint.colliderB) {
                constraint.colliderB->AccumulateImpulse(constraint.normalImpulse);
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

    float ContactSolver::EffectiveInverseMass(const ContactConstraint& constraint,
                                              const Vector3& axis)
    {
        float result = constraint.inverseMassSum;

        // てこの先を軸方向へ動かすときの回りにくさを足す
        if (constraint.bodyA) {
            const Vector3 angular = constraint.bodyA->ApplyInverseInertia(
                Cross(constraint.leverA, axis));
            result += Dot(Cross(angular, constraint.leverA), axis);
        }
        if (constraint.bodyB) {
            const Vector3 angular = constraint.bodyB->ApplyInverseInertia(
                Cross(constraint.leverB, axis));
            result += Dot(Cross(angular, constraint.leverB), axis);
        }
        return result;
    }

    void ContactSolver::ApplyAxisImpulse(ContactConstraint& constraint, const Vector3& axis,
                                         float& accumulated, float minImpulse, float maxImpulse,
                                         float bias)
    {
        const float inverseMass = EffectiveInverseMass(constraint, axis);
        if (inverseMass <= 0.0f) {
            return;
        }

        const Vector3 velocityA = constraint.bodyA
            ? constraint.bodyA->GetVelocityAtPoint(constraint.point) : Vector3{};
        const Vector3 velocityB = constraint.bodyB
            ? constraint.bodyB->GetVelocityAtPoint(constraint.point) : Vector3{};

        // axis は a から b へ向かう向き。正で近づく
        const float approachSpeed = Dot(velocityA - velocityB, axis);

        float impulse = (approachSpeed + bias) / inverseMass;

        // 累積を範囲へ収めてから、その差分だけ与える
        const float previous = accumulated;
        accumulated = previous + impulse;
        if (accumulated < minImpulse) { accumulated = minImpulse; }
        if (accumulated > maxImpulse) { accumulated = maxImpulse; }
        impulse = accumulated - previous;

        if (impulse == 0.0f) {
            return;
        }

        // a は軸の逆向き、b は軸の向きへ。回転は接触点のてこで決まる
        if (constraint.bodyA) {
            constraint.bodyA->AddImpulseAtPoint(axis * -impulse, constraint.point);
        }
        if (constraint.bodyB) {
            constraint.bodyB->AddImpulseAtPoint(axis * impulse, constraint.point);
        }
    }
}
