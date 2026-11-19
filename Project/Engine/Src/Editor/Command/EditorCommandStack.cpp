#include "pch.h"
#include "Editor/Command/EditorCommandStack.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace CoreEngine::Editor
{
    namespace
    {
        /// @brief 操作と、その操作に続いて変わった相手の戻す処理を、1 回の Undo で戻すコマンド
        class FollowUpCommand final : public IEditorCommand
        {
        public:
            explicit FollowUpCommand(std::unique_ptr<IEditorCommand> inner) : inner_(std::move(inner)) {}

            bool Has(const void* key) const
            {
                return std::any_of(followUps_.begin(), followUps_.end(),
                    [key](const FollowUp& followUp) { return followUp.key == key; });
            }

            void Add(const void* key, std::function<void()> restore)
            {
                followUps_.push_back({ key, std::move(restore) });
            }

            void Undo() override
            {
                // 続いて変わった相手を先に戻し、操作そのものが変えた値は操作の Undo で戻す
                for (auto it = followUps_.rbegin(); it != followUps_.rend(); ++it) {
                    if (it->restore) { it->restore(); }
                }
                inner_->Undo();
            }

            void Redo() override { inner_->Redo(); }
            std::string GetLabel() const override { return inner_->GetLabel(); }
            bool References(const void* target) const override { return inner_->References(target); }
            bool AffectsScene() const override { return inner_->AffectsScene(); }
            bool SurvivesSceneReload() const override { return inner_->SurvivesSceneReload(); }

        private:
            struct FollowUp
            {
                const void* key = nullptr;
                std::function<void()> restore;
            };

            std::unique_ptr<IEditorCommand> inner_;
            std::vector<FollowUp> followUps_;
        };
    }

    EditorCommandStack& EditorCommandStack::Get()
    {
        static EditorCommandStack instance;
        return instance;
    }

    void EditorCommandStack::Push(std::unique_ptr<IEditorCommand> command)
    {
        if (!command) {
            return;
        }

        // Undo / Redo の適用が起こした変更を履歴へ積み直すと、同じ操作が無限に増える
        if (applying_ || discardDepth_ > 0) {
            return;
        }

        if (batch_) {
            if (command->AffectsScene()) { ++sceneRevision_; }
            batch_->Add(std::move(command));
            return;
        }

        PushToUndo(std::move(command));
        redo_.clear();
    }

    void EditorCommandStack::PushToUndo(std::unique_ptr<IEditorCommand> command)
    {
        if (command->AffectsScene()) { ++sceneRevision_; }
        undo_.push_back(std::move(command));
        TrimUndo();
    }

    void EditorCommandStack::TrimUndo()
    {
        if (undo_.size() > kMaxCommands) {
            undo_.erase(undo_.begin(), undo_.begin() + static_cast<std::ptrdiff_t>(undo_.size() - kMaxCommands));
        }
    }

    bool EditorCommandStack::Undo()
    {
        if (undo_.empty()) {
            return false;
        }

        std::unique_ptr<IEditorCommand> command = std::move(undo_.back());
        undo_.pop_back();

        applying_ = true;
        command->Undo();
        applying_ = false;

        if (command->AffectsScene()) { ++sceneRevision_; }
        redo_.push_back(std::move(command));
        return true;
    }

    bool EditorCommandStack::Redo()
    {
        if (redo_.empty()) {
            return false;
        }

        std::unique_ptr<IEditorCommand> command = std::move(redo_.back());
        redo_.pop_back();

        applying_ = true;
        command->Redo();
        applying_ = false;

        PushToUndo(std::move(command));
        return true;
    }

    bool EditorCommandStack::CanAttachToLast(const void* key) const
    {
        if (undo_.empty() || inPlaySession_ || batchDepth_ > 0 || applying_ || discardDepth_ > 0) {
            return false;
        }
        const auto* const followUp = dynamic_cast<const FollowUpCommand*>(undo_.back().get());
        return !followUp || !followUp->Has(key);
    }

    void EditorCommandStack::AttachToLast(const void* key, std::function<void()> restore)
    {
        if (!CanAttachToLast(key)) {
            return;
        }
        auto* followUp = dynamic_cast<FollowUpCommand*>(undo_.back().get());
        if (!followUp) {
            auto wrapped = std::make_unique<FollowUpCommand>(std::move(undo_.back()));
            followUp = wrapped.get();
            undo_.back() = std::move(wrapped);
        }
        followUp->Add(key, std::move(restore));
    }

    std::string EditorCommandStack::PeekUndoLabel() const
    {
        return undo_.empty() ? std::string{} : undo_.back()->GetLabel();
    }

    std::string EditorCommandStack::PeekRedoLabel() const
    {
        return redo_.empty() ? std::string{} : redo_.back()->GetLabel();
    }

    void EditorCommandStack::Clear()
    {
        undo_.clear();
        redo_.clear();
        batch_.reset();
        batchDepth_ = 0;
    }

    void EditorCommandStack::RemoveCommandsReferencing(const void* target)
    {
        if (!target) {
            return;
        }

        auto drop = [target](std::vector<std::unique_ptr<IEditorCommand>>& stack) {
            stack.erase(
                std::remove_if(stack.begin(), stack.end(),
                    [target](const std::unique_ptr<IEditorCommand>& command) {
                        return command && command->References(target);
                    }),
                stack.end());
            };

        drop(undo_);
        drop(redo_);
        drop(pausedUndo_);
        drop(pausedRedo_);
        if (batch_ && batch_->References(target)) {
            batch_.reset();
            batch_ = std::make_unique<CompositeCommand>("");
        }
    }

    void EditorCommandStack::BeginPlaySession()
    {
        if (inPlaySession_) {
            return;
        }

        batch_.reset();
        batchDepth_ = 0;
        pausedUndo_ = std::move(undo_);
        pausedRedo_ = std::move(redo_);
        undo_.clear();
        redo_.clear();
        inPlaySession_ = true;
    }

    EditorCommandStack::History EditorCommandStack::EndPlaySession()
    {
        History history;
        if (inPlaySession_) {
            history.undo = std::move(pausedUndo_);
            history.redo = std::move(pausedRedo_);

            // 再生中の操作は、シーンを変えないものだけを後ろへ続ける
            bool appended = false;
            for (auto& command : undo_) {
                if (command && !command->AffectsScene()) {
                    history.undo.push_back(std::move(command));
                    appended = true;
                }
            }
            if (appended) {
                history.redo.clear();
            }
        } else {
            history.undo = std::move(undo_);
            history.redo = std::move(redo_);
        }

        undo_.clear();
        redo_.clear();
        pausedUndo_.clear();
        pausedRedo_.clear();
        batch_.reset();
        batchDepth_ = 0;
        inPlaySession_ = false;

        // シーンを組み直すと使えなくなる操作を外す
        const auto keepSurvivors = [](std::vector<std::unique_ptr<IEditorCommand>>& stack) {
            stack.erase(
                std::remove_if(stack.begin(), stack.end(),
                    [](const std::unique_ptr<IEditorCommand>& command) {
                        return !command || !command->SurvivesSceneReload();
                    }),
                stack.end());
            };
        keepSurvivors(history.undo);
        keepSurvivors(history.redo);
        return history;
    }

    EditorCommandStack::History EditorCommandStack::TakeHistory()
    {
        batch_.reset();
        batchDepth_ = 0;
        History history;
        history.undo = std::move(undo_);
        history.redo = std::move(redo_);
        undo_.clear();
        redo_.clear();
        return history;
    }

    void EditorCommandStack::RestoreHistory(History history)
    {
        if (!undo_.empty()) {
            history.redo.clear();
        }
        for (auto& command : undo_) {
            history.undo.push_back(std::move(command));
        }
        undo_ = std::move(history.undo);
        redo_ = std::move(history.redo);
        TrimUndo();
    }

    void EditorCommandStack::BeginBatch(std::string label)
    {
        ++batchDepth_;
        if (batchDepth_ == 1) {
            batch_ = std::make_unique<CompositeCommand>(std::move(label));
        }
    }

    void EditorCommandStack::EndBatch()
    {
        if (batchDepth_ <= 0) {
            return;
        }

        --batchDepth_;
        if (batchDepth_ > 0) {
            return;
        }

        std::unique_ptr<CompositeCommand> batch = std::move(batch_);
        batch_.reset();
        if (!batch || batch->IsEmpty()) {
            return;
        }

        // 1 件だけなら包む意味が無いので中身をそのまま積む（履歴の名前も本来のものになる）
        if (std::unique_ptr<IEditorCommand> single = batch->ExtractSingle()) {
            PushToUndo(std::move(single));
        } else {
            PushToUndo(std::move(batch));
        }
        redo_.clear();
    }
}
