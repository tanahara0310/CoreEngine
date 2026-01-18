#pragma once

#include <string>

/// @brief ボスアクションの基底クラス（FSM用）
class BossAction {
public:
    /// @brief アクションの実行状態
    enum class State {
        Idle,       // 待機中
        Running,    // 実行中
        Completed   // 完了
    };

    virtual ~BossAction() = default;

    /// @brief アクションを開始
    virtual void Start() {
        state_ = State::Running;
    }

    /// @brief アクションを更新
    /// @param deltaTime デルタタイム
    virtual void Update(float deltaTime) = 0;

    /// @brief アクションを終了
    virtual void End() {
        state_ = State::Completed;
    }

    /// @brief アクションの状態を取得
    /// @return アクションの状態
    State GetState() const { return state_; }

    /// @brief アクションが完了したか確認
    /// @return 完了している場合true
    bool IsCompleted() const { return state_ == State::Completed; }

    /// @brief アクションがアクティブか確認
    /// @return アクティブな場合true
    bool IsActive() const { return state_ == State::Running; }

    /// @brief アクション名を取得
    /// @return アクション名
    virtual const char* GetName() const = 0;

protected:
    State state_ = State::Idle;
};
