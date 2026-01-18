#pragma once

#include "Application/AL3/AI/BehaviorTree/BehaviorNode.h"
#include "Action/BossActionManager.h"
#include <memory>
#include <string>

// 前方宣言
class BossObject;
class PlayerObject;

/// @brief ボスAI管理クラス（ビヘイビアツリー + FSM のハイブリッド）
class BossAI {
public:
    /// @brief 初期化
    /// @param boss ボスオブジェクト
    /// @param player プレイヤーオブジェクト
    void Initialize(BossObject* boss, PlayerObject* player);

    /// @brief 更新
    /// @param deltaTime デルタタイム
    void Update(float deltaTime);

    /// @brief デバッグ情報を取得
    /// @return 現在の状態を表す文字列
    const char* GetDebugInfo() const;

    /// @brief 現在実行中のアクション名を取得
    /// @return アクション名（なければ"None"）
    std::string GetCurrentActionName() const;

    /// @brief ビヘイビアツリーの最後の実行結果を取得
    /// @return 実行結果（Success/Failure/Running）
    std::string GetLastTreeStatus() const;

    /// @brief 総実行時間を取得
    /// @return 総実行時間（秒）
    float GetTotalTime() const { return totalTime_; }

    /// @brief ビヘイビアツリーのルートノードを取得
    /// @return ルートノード
    BehaviorNode* GetBehaviorTreeRoot() const { return behaviorTreeRoot_.get(); }

    /// @brief アクションマネージャーを取得
    /// @return アクションマネージャー
    BossActionManager* GetActionManager() { return &actionManager_; }

    /// @brief プレイヤーとの距離を取得
    /// @return プレイヤーとの距離
    float GetDistanceToPlayer() const;

private:
    /// @brief ビヘイビアツリーを構築
    void BuildBehaviorTree();

    BossObject* boss_ = nullptr;
    PlayerObject* player_ = nullptr;
    BossActionManager actionManager_;
    std::unique_ptr<BehaviorNode> behaviorTreeRoot_;
    
    // デバッグ用
    float totalTime_ = 0.0f;
    BehaviorNode::Status lastTreeStatus_ = BehaviorNode::Status::Success;
};
