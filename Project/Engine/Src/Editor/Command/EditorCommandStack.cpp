#include "pch.h"
#include "Editor/Command/EditorCommandStack.h"

#include <algorithm>

namespace CoreEngine::Editor
{
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
        if (applying_) {
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
        if (undo_.size() > kMaxCommands) {
            undo_.erase(undo_.begin());
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
        if (batch_ && batch_->References(target)) {
            batch_.reset();
            batch_ = std::make_unique<CompositeCommand>("");
        }
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
