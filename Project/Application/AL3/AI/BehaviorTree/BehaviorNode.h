#pragma once

/// @brief ビヘイビアツリーのノード基底クラス
class BehaviorNode {
public:
    /// @brief ノードの実行状態
    enum class Status {
        Success,  // 成功
        Failure,  // 失敗
        Running   // 実行中
    };

    virtual ~BehaviorNode() = default;

    /// @brief ノードを実行
    /// @param deltaTime デルタタイム
    /// @return 実行状態
    virtual Status Execute(float deltaTime) = 0;

    /// @brief ノードをリセット
    virtual void Reset() {}

protected:
    BehaviorNode() = default;
};
