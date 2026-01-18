#include "BehaviorTreeNodes.h"
#include "Application/AL3/AI/Action/BossActionManager.h"
#include <random>

// ===== ActionNode =====
ActionNode::ActionNode(BossActionManager* actionManager, const std::string& actionName)
    : actionManager_(actionManager), actionName_(actionName) {}

BehaviorNode::Status ActionNode::Execute([[maybe_unused]] float deltaTime) {
    // 初回実行時にアクションを開始
    if (!hasStarted_) {
        if (!actionManager_->ExecuteAction(actionName_)) {
            return Status::Failure;
        }
        hasStarted_ = true;
    }

    // アクションが完了するまで待機
    if (actionManager_->IsCurrentActionCompleted()) {
        hasStarted_ = false;
        return Status::Success;
    }

    return Status::Running;
}

void ActionNode::Reset() {
    hasStarted_ = false;
}

// ===== ConditionNode =====
ConditionNode::ConditionNode(ConditionFunc condition)
    : condition_(condition) {}

BehaviorNode::Status ConditionNode::Execute([[maybe_unused]] float deltaTime) {
    return condition_() ? Status::Success : Status::Failure;
}

// ===== RandomNode =====
RandomNode::RandomNode(float successProbability)
    : successProbability_(successProbability) {}

BehaviorNode::Status RandomNode::Execute([[maybe_unused]] float deltaTime) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dis(0.0f, 1.0f);
    
    return dis(gen) <= successProbability_ ? Status::Success : Status::Failure;
}
