#pragma once

#include "Application/AL3/AI/BehaviorTree/BehaviorNode.h"
#include <functional>
#include <string>

// 前方宣言
class BossActionManager;

/// @brief アクション実行ノード（BehaviorTree → FSM の橋渡し）
class ActionNode : public BehaviorNode {
public:
    /// @brief コンストラクタ
    /// @param actionManager アクションマネージャー
    /// @param actionName 実行するアクション名
    ActionNode(BossActionManager* actionManager, const std::string& actionName);

    Status Execute(float deltaTime) override;
    void Reset() override;

private:
    BossActionManager* actionManager_;
    std::string actionName_;
    bool hasStarted_ = false;
};

/// @brief 条件ノード（条件判定を行う）
class ConditionNode : public BehaviorNode {
public:
    using ConditionFunc = std::function<bool()>;

    /// @brief コンストラクタ
    /// @param condition 条件判定関数
    ConditionNode(ConditionFunc condition);

    Status Execute(float deltaTime) override;

private:
    ConditionFunc condition_;
};

/// @brief ランダム選択ノード（指定確率で成功）
class RandomNode : public BehaviorNode {
public:
    /// @brief コンストラクタ
    /// @param successProbability 成功確率（0.0～1.0）
    RandomNode(float successProbability = 0.5f);

    Status Execute(float deltaTime) override;

private:
    float successProbability_;
};
