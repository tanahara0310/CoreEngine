#ifdef _DEBUG

#include "UndoRedoHistory.h"
#include "Engine/ObjectCommon/GameObjectManager.h"
#include "Engine/ObjectCommon/GameObject.h"
#include "Engine/ObjectCommon/SpriteObject.h"

namespace CoreEngine
{
    void UndoRedoHistory::Push(const TransformRecord& record)
    {
        // before と after が同じなら記録しない
        auto eq3 = [](const Vector3& a, const Vector3& b) {
            return a.x == b.x && a.y == b.y && a.z == b.z;
        };
        if (eq3(record.translateBefore, record.translateAfter) &&
            eq3(record.rotateBefore,    record.rotateAfter)    &&
            eq3(record.scaleBefore,     record.scaleAfter)     &&
            record.activeBefore == record.activeAfter) {
            return;
        }

        // 新しい操作で分岐するので Redo スタックをクリア
        redoStack_.clear();

        undoStack_.push_back(record);

        // 上限を超えたら最古の履歴を削除
        if (static_cast<int>(undoStack_.size()) > kMaxSteps) {
            undoStack_.erase(undoStack_.begin());
        }
    }

    bool UndoRedoHistory::Undo(GameObjectManager* manager)
    {
        if (undoStack_.empty()) return false;

        TransformRecord record = undoStack_.back();
        undoStack_.pop_back();

        ApplyState(manager, record.objectName,
                   record.translateBefore, record.rotateBefore,
                   record.scaleBefore, record.activeBefore);

        redoStack_.push_back(record);
        return true;
    }

    bool UndoRedoHistory::Redo(GameObjectManager* manager)
    {
        if (redoStack_.empty()) return false;

        TransformRecord record = redoStack_.back();
        redoStack_.pop_back();

        ApplyState(manager, record.objectName,
                   record.translateAfter, record.rotateAfter,
                   record.scaleAfter, record.activeAfter);

        undoStack_.push_back(record);
        return true;
    }

    void UndoRedoHistory::Clear()
    {
        undoStack_.clear();
        redoStack_.clear();
    }

    void UndoRedoHistory::ApplyState(GameObjectManager* manager, const std::string& name,
                                      const Vector3& translate, const Vector3& rotate,
                                      const Vector3& scale, bool active)
    {
        if (!manager) return;

        for (const auto& obj : manager->GetAllObjects()) {
            if (obj && obj->GetName() == name) {
                // SpriteObject の場合は独自トランスフォームに適用
                auto* spriteObj = dynamic_cast<SpriteObject*>(obj.get());
                if (spriteObj) {
                    spriteObj->GetSpriteTransform().translate = translate;
                    spriteObj->GetSpriteTransform().rotate    = rotate;
                    spriteObj->GetSpriteTransform().scale     = scale;
                } else {
                    obj->GetTransform().translate = translate;
                    obj->GetTransform().rotate    = rotate;
                    obj->GetTransform().scale     = scale;
                }
                obj->SetActive(active);
                break;
            }
        }
    }
}

#endif // _DEBUG
