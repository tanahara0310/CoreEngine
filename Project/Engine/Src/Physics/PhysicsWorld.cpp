#include "pch.h"
#include "PhysicsWorld.h"
#include "RigidbodyComponent.h"

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

    //================================================
    // ステップ
    //================================================

    void PhysicsWorld::Step(float fixedDeltaTime)
    {
        if (preStep_) {
            preStep_(fixedDeltaTime);
        }

        for (RigidbodyComponent* body : bodies_) {
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

        for (RigidbodyComponent* body : bodies_) {
            body->IntegratePosition(fixedDeltaTime);
            body->IntegrateRotation(fixedDeltaTime);
        }

        solver_.SolvePositions(correctionRate_, penetrationSlop_);

        simulatedTime_ += fixedDeltaTime;
    }
}
