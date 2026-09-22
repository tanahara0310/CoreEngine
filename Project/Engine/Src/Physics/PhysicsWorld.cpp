#include "pch.h"
#include "PhysicsWorld.h"
#include "RigidbodyComponent.h"

#include "Collision/Collider.h"
#include "GameObject/GameObject.h"

namespace CoreEngine
{
    int PhysicsWorld::Advance(float deltaTime)
    {
        lastStepCount_ = 0;

        if (deltaTime > 0.0f) {
            accumulator_ += deltaTime;
        }

        while (accumulator_ >= fixedDeltaTime_ && lastStepCount_ < maxSubSteps_) {
            Step(fixedDeltaTime_);
            accumulator_ -= fixedDeltaTime_;
            ++lastStepCount_;
            ++totalStepCount_;
        }

        // 上限に達してまだ 1 ステップ分以上残っているときは、その残りを捨てる
        if (accumulator_ >= fixedDeltaTime_) {
            droppedTime_ += accumulator_;
            accumulator_ = 0.0f;
        }

        return lastStepCount_;
    }

    void PhysicsWorld::Reset()
    {
        accumulator_ = 0.0f;
        lastStepCount_ = 0;
        totalStepCount_ = 0;
        simulatedTime_ = 0.0f;
        droppedTime_ = 0.0f;
        bodies_.clear();
        contacts_.clear();
        collisionWorld_ = nullptr;
    }

    //================================================
    // 剛体
    //================================================

    void PhysicsWorld::ClearBodies()
    {
        // capacity は残す（毎フレーム同じくらいの件数を入れ直すため）
        bodies_.clear();
    }

    void PhysicsWorld::RegisterBody(RigidbodyComponent* body)
    {
        if (body) {
            bodies_.push_back(body);
        }
    }

    //================================================
    // 設定
    //================================================

    void PhysicsWorld::SetFixedDeltaTime(float seconds)
    {
        if (seconds > 0.0f) {
            fixedDeltaTime_ = seconds;
        }
    }

    void PhysicsWorld::SetMaxSubSteps(int count)
    {
        maxSubSteps_ = (count > 1) ? count : 1;
    }

    void PhysicsWorld::SetCorrectionRate(float rate)
    {
        correctionRate_ = (rate < 0.0f) ? 0.0f : ((rate > 1.0f) ? 1.0f : rate);
    }

    void PhysicsWorld::SetPenetrationSlop(float slop)
    {
        penetrationSlop_ = (slop > 0.0f) ? slop : 0.0f;
    }

    //================================================
    // ステップ
    //================================================

    void PhysicsWorld::Step(float fixedDeltaTime)
    {
        for (RigidbodyComponent* body : bodies_) {
            body->IntegrateVelocity(gravity_, fixedDeltaTime);
        }

        if (collisionWorld_) {
            collisionWorld_->CollectContacts(contacts_);
            SolveVelocities();
        } else {
            contacts_.clear();
        }

        for (RigidbodyComponent* body : bodies_) {
            body->IntegratePosition(fixedDeltaTime);
        }

        SolvePositions();

        simulatedTime_ += fixedDeltaTime;
    }

    void PhysicsWorld::SolveVelocities()
    {
        for (const ContactPair& pair : contacts_) {
            if (pair.a->IsTrigger() || pair.b->IsTrigger()) {
                continue;
            }

            RigidbodyComponent* const bodyA = FindBody(pair.a);
            RigidbodyComponent* const bodyB = FindBody(pair.b);

            const float inverseMassA = bodyA ? bodyA->GetInverseMass() : 0.0f;
            const float inverseMassB = bodyB ? bodyB->GetInverseMass() : 0.0f;
            const float inverseMassSum = inverseMassA + inverseMassB;
            if (inverseMassSum <= 0.0f) {
                continue;
            }

            const Vector3 velocityA = bodyA ? bodyA->GetVelocity() : Vector3{};
            const Vector3 velocityB = bodyB ? bodyB->GetVelocity() : Vector3{};

            // normal は a から b へ向かう。a が +normal へ動くほど深くめり込む
            const float approachSpeed = Dot(velocityA - velocityB, pair.contact.normal);
            if (approachSpeed <= 0.0f) {
                continue;   // 離れつつある
            }

            // 反発なし（完全非弾性）。近づく向きの相対速度をちょうど 0 にする
            const float impulse = -approachSpeed / inverseMassSum;

            if (bodyA) {
                bodyA->SetVelocity(velocityA + pair.contact.normal * (impulse * inverseMassA));
            }
            if (bodyB) {
                bodyB->SetVelocity(velocityB - pair.contact.normal * (impulse * inverseMassB));
            }
        }
    }

    void PhysicsWorld::SolvePositions()
    {
        for (const ContactPair& pair : contacts_) {
            if (pair.a->IsTrigger() || pair.b->IsTrigger()) {
                continue;
            }

            const float excess = pair.contact.depth - penetrationSlop_;
            if (excess <= 0.0f) {
                continue;   // 許容の範囲なので触らない
            }

            RigidbodyComponent* const bodyA = FindBody(pair.a);
            RigidbodyComponent* const bodyB = FindBody(pair.b);

            const float inverseMassA = bodyA ? bodyA->GetInverseMass() : 0.0f;
            const float inverseMassB = bodyB ? bodyB->GetInverseMass() : 0.0f;
            const float inverseMassSum = inverseMassA + inverseMassB;
            if (inverseMassSum <= 0.0f) {
                continue;
            }

            // 軽い方が多く動くよう、質量の逆数の比で割り振る
            const float correction = excess * correctionRate_ / inverseMassSum;

            if (bodyA) {
                bodyA->ApplyPositionDelta(pair.contact.normal * (-correction * inverseMassA));
            }
            if (bodyB) {
                bodyB->ApplyPositionDelta(pair.contact.normal * (correction * inverseMassB));
            }
        }
    }

    RigidbodyComponent* PhysicsWorld::FindBody(const Collider* collider)
    {
        GameObject* const owner = collider ? collider->GetOwner() : nullptr;
        if (!owner) {
            return nullptr;
        }

        RigidbodyComponent* const body = owner->GetComponent<RigidbodyComponent>();
        return (body && body->IsEnabled()) ? body : nullptr;
    }
}
