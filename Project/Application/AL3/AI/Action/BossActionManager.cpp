#include "BossActionManager.h"
#include "Application/AL3/GameObject/BossObject.h"

using namespace CoreEngine;

void BossActionManager::Initialize(BossObject* boss) {
    boss_ = boss;
    currentAction_ = nullptr;
}

void BossActionManager::Update(float deltaTime) {
    if (!currentAction_) {
        return;
    }

    // 現在のアクションを更新
    if (currentAction_->IsActive()) {
        currentAction_->Update(deltaTime);
    }

    // アクションが完了したら終了処理
    if (currentAction_->IsCompleted()) {
        currentAction_->End();
        currentAction_ = nullptr;
    }
}

bool BossActionManager::ExecuteAction(const std::string& actionName) {
    // 既にアクションが実行中の場合は失敗
    if (currentAction_ && currentAction_->IsActive()) {
        return false;
    }

    // アクションを検索
    auto it = actions_.find(actionName);
    if (it == actions_.end()) {
        return false;
    }

    // アクションを開始
    currentAction_ = it->second.get();
    currentAction_->Start();
    return true;
}

bool BossActionManager::IsCurrentActionCompleted() const {
    if (!currentAction_) {
        return true;
    }
    return currentAction_->IsCompleted();
}

void BossActionManager::RegisterAction(const std::string& name, std::unique_ptr<BossAction> action) {
    actions_[name] = std::move(action);
}
