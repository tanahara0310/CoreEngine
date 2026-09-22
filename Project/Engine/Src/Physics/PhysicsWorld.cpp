#include "pch.h"
#include "PhysicsWorld.h"

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
    }

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

    void PhysicsWorld::Step(float fixedDeltaTime)
    {
        simulatedTime_ += fixedDeltaTime;
    }
}
