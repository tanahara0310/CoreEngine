#include "pch.h"
#ifdef USE_IMGUI

#include "UndoRedoHistory.h"
#include "ObjectCommon/GameObjectManager.h"
#include "ObjectCommon/GameObject.h"
#include "ObjectCommon/Model/DynamicModelObject.h"
#include "Graphics/Model/Model.h"
#include "Graphics/Material/MaterialInstance.h"
#include "Utility/Debug/ImGui/GameObjectDebugAccess.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    // ===== スタックへの共通追加処理 =====

    void UndoRedoHistory::PushToStack(std::vector<HistoryEntry>& stack, HistoryEntry entry)
    {
        stack.push_back(std::move(entry));
        if (static_cast<int>(stack.size()) > kMaxSteps) {
            stack.erase(stack.begin());
        }
    }

    // ===== トランスフォーム変更の記録 =====

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
        PushToStack(undoStack_, record);
    }

    // ===== スポーン操作の記録 =====

    void UndoRedoHistory::Push(const ObjectSpawnRecord& record)
    {
        // 新しい操作で分岐するので Redo スタックをクリア
        redoStack_.clear();
        PushToStack(undoStack_, record);
    }

    // ===== Undo =====

    bool UndoRedoHistory::Undo(GameObjectManager* manager)
    {
        if (undoStack_.empty()) return false;

        HistoryEntry entry = undoStack_.back();
        undoStack_.pop_back();

        std::visit([&](auto&& e) {
            using T = std::decay_t<decltype(e)>;

            if constexpr (std::is_same_v<T, TransformRecord>) {
                // トランスフォームを「変更前」に戻す
                ApplyTransform(manager, e.objectName,
                               e.translateBefore, e.rotateBefore,
                               e.scaleBefore, e.activeBefore);

            } else if constexpr (std::is_same_v<T, ObjectSpawnRecord>) {
                // 削除前コールバック（ObjectSelector の選択解除など）
                if (onBeforeDestroy_) {
                    onBeforeDestroy_(e.objectName);
                }
                // スポーンを Undo → 生成されたオブジェクトを削除する
                manager->DestroyByName(e.objectName);
                Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System,
                    "Undo: オブジェクトを削除しました: {}", e.objectName);
            }
        }, entry);

        redoStack_.push_back(std::move(entry));
        return true;
    }

    // ===== Redo =====

    bool UndoRedoHistory::Redo(GameObjectManager* manager)
    {
        if (redoStack_.empty()) return false;

        HistoryEntry entry = redoStack_.back();
        redoStack_.pop_back();

        std::visit([&](auto&& e) {
            using T = std::decay_t<decltype(e)>;

            if constexpr (std::is_same_v<T, TransformRecord>) {
                // トランスフォームを「変更後」に戻す
                ApplyTransform(manager, e.objectName,
                               e.translateAfter, e.rotateAfter,
                               e.scaleAfter, e.activeAfter);

            } else if constexpr (std::is_same_v<T, ObjectSpawnRecord>) {
                // スポーンを Redo → 同じオブジェクトを再生成する
                auto newObj = std::make_unique<DynamicModelObject>();
                newObj->SetModelPath(e.modelPath);
                newObj->SetName(e.objectName);
                DynamicModelObject* raw = manager->AddObject(std::move(newObj));
                if (raw) {
                    if (Model* model = raw->GetModel()) {
                        if (MaterialInstance* material = model->GetMaterial()) {
                            material->SetLightingEnabled(true);
                            material->SetIBLEnabled(true);
                            material->SetNormalMapEnabled(false);
                        }
                    }
                    DebugAccess::TransformAccess access;
                    if (DebugAccess::TryGetTransformAccess(raw, access)) {
                        *access.translate = e.translate;
                        *access.rotate    = e.rotate;
                        *access.scale     = e.scale;
                    }
                    Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System,
                        "Redo: オブジェクトを再生成しました: {}", e.objectName);
                }
            }
        }, entry);

        undoStack_.push_back(std::move(entry));
        return true;
    }

    void UndoRedoHistory::Clear()
    {
        undoStack_.clear();
        redoStack_.clear();
    }

    void UndoRedoHistory::ApplyTransform(GameObjectManager* manager, const std::string& name,
                                          const Vector3& translate, const Vector3& rotate,
                                          const Vector3& scale, bool active)
    {
        if (!manager) return;

        for (const auto& obj : manager->GetAllObjects()) {
            if (obj && obj->GetName() == name) {
                DebugAccess::TransformAccess access;
                if (DebugAccess::TryGetTransformAccess(obj.get(), access)) {
                    *access.translate = translate;
                    *access.rotate = rotate;
                    *access.scale = scale;
                }
                obj->SetActive(active);
                break;
            }
        }
    }
}

#endif // USE_IMGUI
