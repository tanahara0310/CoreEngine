#include "BossAI.h"
#include "Application/AL3/GameObject/BossObject.h"
#include "Application/AL3/GameObject/PlayerObject.h"
#include "BehaviorTree/CompositeNode.h"
#include "BehaviorTree/BehaviorTreeNodes.h"
#include "Action/BossActions.h"
#include "Engine/Math/MathCore.h"

void BossAI::Initialize(BossObject* boss, PlayerObject* player) {
    boss_ = boss;
    player_ = player;
    
    // アクションマネージャーを初期化
    actionManager_.Initialize(boss);
    
    // 各アクションを登録
    actionManager_.RegisterAction("Idle", std::make_unique<IdleAction>(boss, 2.0f));
    actionManager_.RegisterAction("Walk", std::make_unique<WalkAction>(boss, 3.0f));
    actionManager_.RegisterAction("JumpAttack", std::make_unique<JumpAttackAction>(boss));
    actionManager_.RegisterAction("PunchAttack", std::make_unique<PunchAttackAction>(boss));
    actionManager_.RegisterAction("DashAttack", std::make_unique<DashAttackAction>(boss));
    actionManager_.RegisterAction("CircularBarrage", std::make_unique<CircularBarrageAction>(boss, 12));
    
    // ビヘイビアツリーを構築
    BuildBehaviorTree();
}

void BossAI::Update(float deltaTime) {
    totalTime_ += deltaTime;
    
    // アクションマネージャーを更新（FSM部分）
    actionManager_.Update(deltaTime);
    
    // ビヘイビアツリーを実行（戦術レベルの意思決定）
    if (behaviorTreeRoot_) {
        lastTreeStatus_ = behaviorTreeRoot_->Execute(deltaTime);
    }
}

void BossAI::BuildBehaviorTree() {
    // ビヘイビアツリーの構築（距離判定を含む）
    // 構造:
    // Root(Selector)
    //   ├── Sequence: 近距離パンチ攻撃
    //   │     ├── Condition: プレイヤーが近い（3.0未満）
    //   │     ├── Random(70%確率)
    //   │     └── ActionNode(PunchAttack)
    //   ├── Sequence: 中距離突進攻撃
    //   │     ├── Condition: プレイヤーが中距離（5.0～12.0）
    //   │     ├── Random(35%確率)
    //   │     └── ActionNode(DashAttack)
    //   ├── Sequence: 中距離円形弾幕攻撃
    //   │     ├── Condition: プレイヤーが中距離（8.0～15.0）
    //   │     ├── Random(30%確率)
    //   │     └── ActionNode(CircularBarrage)
    //   ├── Sequence: 中距離ジャンプ攻撃
    //   │     ├── Condition: プレイヤーが中距離（3.0～8.0）
    //   │     ├── Random(40%確率)
    //   │     └── ActionNode(JumpAttack)
    //   ├── Sequence: 中距離パンチ攻撃
    //   │     ├── Condition: プレイヤーが中距離（3.0～8.0）
    //   │     ├── Random(25%確率)
    //   │     └── ActionNode(PunchAttack)
    //   ├── Sequence: 遠距離接近
    //   │     ├── Condition: プレイヤーが遠い（8.0以上）
    //   │     └── ActionNode(Walk)
    //   └── ActionNode(Idle) ← デフォルト行動

    auto root = std::make_unique<SelectorNode>();

    // ===== 近距離: パンチ攻撃パターン =====
    auto closeRangePunchSequence = std::make_unique<SequenceNode>();
    // 距離判定: 3.0未満ならパンチ攻撃
    closeRangePunchSequence->AddChild(std::make_unique<ConditionNode>([this]() {
        return GetDistanceToPlayer() < 3.0f;
    }));
    // 70%の確率でパンチ攻撃（攻撃頻度を上げる）
    closeRangePunchSequence->AddChild(std::make_unique<RandomNode>(0.7f));
    closeRangePunchSequence->AddChild(std::make_unique<ActionNode>(&actionManager_, "PunchAttack"));
    root->AddChild(std::move(closeRangePunchSequence));

    // ===== 中距離: 突進攻撃パターン =====
    auto midRangeDashSequence = std::make_unique<SequenceNode>();
    // 距離判定: 5.0～12.0の範囲なら突進攻撃を試行
    midRangeDashSequence->AddChild(std::make_unique<ConditionNode>([this]() {
        float distance = GetDistanceToPlayer();
        return distance >= 5.0f && distance < 12.0f;
    }));
    // 35%の確率で突進攻撃
    midRangeDashSequence->AddChild(std::make_unique<RandomNode>(0.35f));
    midRangeDashSequence->AddChild(std::make_unique<ActionNode>(&actionManager_, "DashAttack"));
    root->AddChild(std::move(midRangeDashSequence));

    // ===== 中距離: 円形弾幕攻撃パターン =====
    auto midRangeBarrageSequence = std::make_unique<SequenceNode>();
    // 距離判定: 8.0～15.0の範囲なら弾幕攻撃を試行
    midRangeBarrageSequence->AddChild(std::make_unique<ConditionNode>([this]() {
        float distance = GetDistanceToPlayer();
        return distance >= 8.0f && distance < 15.0f;
    }));
    // 30%の確率で弾幕攻撃
    midRangeBarrageSequence->AddChild(std::make_unique<RandomNode>(0.3f));
    midRangeBarrageSequence->AddChild(std::make_unique<ActionNode>(&actionManager_, "CircularBarrage"));
    root->AddChild(std::move(midRangeBarrageSequence));

    // ===== 中距離: ジャンプ攻撃パターン =====
    auto midRangeJumpSequence = std::make_unique<SequenceNode>();
    // 距離判定: 3.0～8.0の範囲ならジャンプ攻撃を試行
    midRangeJumpSequence->AddChild(std::make_unique<ConditionNode>([this]() {
        float distance = GetDistanceToPlayer();
        return distance >= 3.0f && distance < 8.0f;
    }));
    // 40%の確率でジャンプ攻撃（頻度を少し抑える）
    midRangeJumpSequence->AddChild(std::make_unique<RandomNode>(0.4f));
    midRangeJumpSequence->AddChild(std::make_unique<ActionNode>(&actionManager_, "JumpAttack"));
    root->AddChild(std::move(midRangeJumpSequence));

    // ===== 中距離: パンチ攻撃の代替パターン =====
    auto midRangePunchSequence = std::make_unique<SequenceNode>();
    // 距離判定: 3.0～8.0の範囲（ジャンプ攻撃が失敗した場合）
    midRangePunchSequence->AddChild(std::make_unique<ConditionNode>([this]() {
        float distance = GetDistanceToPlayer();
        return distance >= 3.0f && distance < 8.0f;
    }));
    // 25%の確率でパンチ攻撃
    midRangePunchSequence->AddChild(std::make_unique<RandomNode>(0.25f));
    midRangePunchSequence->AddChild(std::make_unique<ActionNode>(&actionManager_, "PunchAttack"));
    root->AddChild(std::move(midRangePunchSequence));

    // ===== 遠距離: 接近パターン =====
    auto longRangeWalkSequence = std::make_unique<SequenceNode>();
    // 距離判定: 8.0以上なら接近
    longRangeWalkSequence->AddChild(std::make_unique<ConditionNode>([this]() {
        return GetDistanceToPlayer() >= 8.0f;
    }));
    longRangeWalkSequence->AddChild(std::make_unique<ActionNode>(&actionManager_, "Walk"));
    root->AddChild(std::move(longRangeWalkSequence));

    // ===== デフォルト: 待機 =====
    root->AddChild(std::make_unique<ActionNode>(&actionManager_, "Idle"));

    behaviorTreeRoot_ = std::move(root);
}

const char* BossAI::GetDebugInfo() const {
    auto currentAction = actionManager_.GetCurrentAction();
    if (currentAction) {
        return currentAction->GetName();
    }
    return "No Action";
}

std::string BossAI::GetCurrentActionName() const {
    auto currentAction = actionManager_.GetCurrentAction();
    if (currentAction) {
        return currentAction->GetName();
    }
    return "None";
}

std::string BossAI::GetLastTreeStatus() const {
    switch (lastTreeStatus_) {
        case BehaviorNode::Status::Success: return "Success";
        case BehaviorNode::Status::Failure: return "Failure";
        case BehaviorNode::Status::Running: return "Running";
        default: return "Unknown";
    }
}

float BossAI::GetDistanceToPlayer() const {
    if (!boss_ || !player_) {
        return 999.0f;  // プレイヤーがいない場合は非常に遠いとみなす
    }

    CoreEngine::Vector3 bossPos = boss_->GetTransform().translate;
    CoreEngine::Vector3 playerPos = player_->GetTransform().translate;

    // 2点間の距離を計算（MathCoreを使用）
    CoreEngine::Vector3 diff = CoreEngine::MathCore::Vector::Subtract(playerPos, bossPos);
    return CoreEngine::MathCore::Vector::Length(diff);
}
