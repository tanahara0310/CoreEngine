#include "pch.h"
#include "PhysicsWorld.h"
#include "RigidbodyComponent.h"

#include "Collision/Collider.h"
#include "Collision/ColliderComponent.h"
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
        preStep_ = nullptr;
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

    void PhysicsWorld::SetContinuousThreshold(float ratio)
    {
        continuousThreshold_ = (ratio > 0.0f) ? ratio : 0.0f;
    }

    void PhysicsWorld::SetSleepThresholds(float linear, float angular, float timeToSleep)
    {
        sleepLinearThreshold_ = (linear > 0.0f) ? linear : 0.0f;
        sleepAngularThreshold_ = (angular > 0.0f) ? angular : 0.0f;
        timeToSleep_ = (timeToSleep > 0.0f) ? timeToSleep : 0.0f;
    }

    size_t PhysicsWorld::GetSleepingCount() const
    {
        size_t count = 0;
        for (const RigidbodyComponent* body : bodies_) {
            if (body->IsSleeping()) {
                ++count;
            }
        }
        return count;
    }

    //================================================
    // ステップ
    //================================================

    void PhysicsWorld::Step(float fixedDeltaTime)
    {
        if (preStep_) {
            preStep_(fixedDeltaTime);
        }

        for (RigidbodyComponent* body : bodies_) {
            if (body->IsSleeping()) {
                continue;
            }
            body->IntegrateVelocity(gravity_, fixedDeltaTime);
            body->IntegrateAngularVelocity(fixedDeltaTime);
        }

        if (collisionWorld_) {
            collisionWorld_->CollectContacts(contacts_);
        } else {
            contacts_.clear();
        }

        solver_.Build(contacts_);
        solver_.WarmStart();
        solver_.SolveVelocities();
        solver_.PublishImpulses();

        for (RigidbodyComponent* body : bodies_) {
            if (body->IsSleeping()) {
                continue;
            }
            if (!continuousEnabled_ || !IntegrateWithSweep(*body, fixedDeltaTime)) {
                body->IntegratePosition(fixedDeltaTime);
            }
            body->IntegrateRotation(fixedDeltaTime);
        }

        solver_.SolvePositions(correctionRate_, penetrationSlop_);

        // 止まったものを計算から外す（接している相手が動けば ContactSolver が起こす）
        if (sleepEnabled_) {
            for (RigidbodyComponent* body : bodies_) {
                body->UpdateSleepState(fixedDeltaTime, sleepLinearThreshold_,
                    sleepAngularThreshold_, timeToSleep_);
            }
        }

        simulatedTime_ += fixedDeltaTime;
    }

    bool PhysicsWorld::IntegrateWithSweep(RigidbodyComponent& body, float fixedDeltaTime)
    {
        GameObject* const owner = body.GetOwner();
        if (!collisionWorld_ || !owner) {
            return false;
        }

        const Vector3 step = body.GetVelocity() * fixedDeltaTime;
        const float distance = Length(step);
        const float extent = body.GetMinimumExtent();

        // 1 ステップの移動が形の薄さに収まっていれば、間を飛び越えようがない
        if (distance <= 0.0f || distance <= extent * continuousThreshold_) {
            return false;
        }

        // 当たる相手はレイヤーの表で決める（自分の最初のコライダーの行）
        const auto* const colliders = owner->GetComponent<ColliderComponent>();
        const Collider* const own = colliders ? colliders->GetFirst() : nullptr;
        const uint64_t layerMask = own ? collisionWorld_->GetLayerMask(own->GetLayer())
                                       : CollisionWorld::kAllLayers;

        const Geometry::Ray ray(owner->GetWorldPosition(), step * (1.0f / distance));

        sweepHits_.clear();
        collisionWorld_->RaycastAll(ray, distance + extent, layerMask, sweepHits_);

        for (const RaycastHit& hit : sweepHits_) {
            // 自分の中から飛ばすので、自分自身と通知専用は読み飛ばす
            if (!hit.collider || hit.object == owner || hit.collider->IsTrigger()) {
                continue;
            }

            // 表面の手前で止める。残ったすき間は次のステップの接触で詰まる
            const float stopDistance = (hit.distance > extent) ? (hit.distance - extent) : 0.0f;
            body.ApplyPositionDelta(ray.direction * stopDistance);

            // 面へ食い込む向きの速度を消す
            const Vector3 velocity = body.GetVelocity();
            const float into = Dot(velocity, hit.normal);
            if (into < 0.0f) {
                body.SetVelocity(velocity - hit.normal * into);
            }
            return true;
        }

        return false;
    }

}
