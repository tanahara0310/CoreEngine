#pragma once

#include "BossAction.h"
#include <memory>
#include <unordered_map>
#include <string>

// 前方宣言
class BossObject;

/// @brief ボスアクションマネージャー（FSMの役割）
class BossActionManager {
public:
    /// @brief 初期化
    /// @param boss ボスオブジェクト
    void Initialize(BossObject* boss);

    /// @brief 更新
    /// @param deltaTime デルタタイム
    void Update(float deltaTime);

    /// @brief アクションを実行
    /// @param actionName アクション名
    /// @return 実行成功ならtrue
    bool ExecuteAction(const std::string& actionName);

    /// @brief 現在のアクションを取得
    /// @return 現在のアクション（なければnullptr）
    BossAction* GetCurrentAction() const { return currentAction_; }

    /// @brief 現在のアクションが完了したか確認
    /// @return 完了している場合true
    bool IsCurrentActionCompleted() const;

    /// @brief アクションを登録
    /// @param name アクション名
    /// @param action アクション
    void RegisterAction(const std::string& name, std::unique_ptr<BossAction> action);

private:
    BossObject* boss_ = nullptr;
    BossAction* currentAction_ = nullptr;
    std::unordered_map<std::string, std::unique_ptr<BossAction>> actions_;
};
