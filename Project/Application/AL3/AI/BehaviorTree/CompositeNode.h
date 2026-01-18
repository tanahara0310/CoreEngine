#pragma once

#include "BehaviorNode.h"
#include <vector>
#include <memory>

/// @brief コンポジットノード基底クラス（複数の子ノードを持つ）
class CompositeNode : public BehaviorNode {
public:
    /// @brief 子ノードを追加
    /// @param child 子ノード
    void AddChild(std::unique_ptr<BehaviorNode> child) {
        children_.push_back(std::move(child));
    }

    void Reset() override {
        for (auto& child : children_) {
            child->Reset();
        }
        currentChildIndex_ = 0;
    }

protected:
    std::vector<std::unique_ptr<BehaviorNode>> children_;
    size_t currentChildIndex_ = 0;
};

/// @brief セレクターノード（優先度順に試行し、成功するまで実行）
class SelectorNode : public CompositeNode {
public:
    Status Execute(float deltaTime) override {
        // すべての子ノードを順番に試行
        for (size_t i = currentChildIndex_; i < children_.size(); ++i) {
            Status status = children_[i]->Execute(deltaTime);

            if (status == Status::Running) {
                currentChildIndex_ = i;
                return Status::Running;
            }

            if (status == Status::Success) {
                currentChildIndex_ = 0;
                return Status::Success;
            }

            // Failureの場合は次の子ノードへ
        }

        // すべての子ノードが失敗
        currentChildIndex_ = 0;
        return Status::Failure;
    }
};

/// @brief シーケンスノード（すべての子ノードを順番に実行）
class SequenceNode : public CompositeNode {
public:
    Status Execute(float deltaTime) override {
        // すべての子ノードを順番に実行
        for (size_t i = currentChildIndex_; i < children_.size(); ++i) {
            Status status = children_[i]->Execute(deltaTime);

            if (status == Status::Running) {
                currentChildIndex_ = i;
                return Status::Running;
            }

            if (status == Status::Failure) {
                currentChildIndex_ = 0;
                return Status::Failure;
            }

            // Successの場合は次の子ノードへ
        }

        // すべての子ノードが成功
        currentChildIndex_ = 0;
        return Status::Success;
    }
};
