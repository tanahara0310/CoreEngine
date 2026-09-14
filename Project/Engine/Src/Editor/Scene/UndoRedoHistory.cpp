#include "pch.h"
#ifdef USE_IMGUI

#include "UndoRedoHistory.h"
#include "Editor/Command/EditorCommandStack.h"
#include "GameObject/GameObjectManager.h"
#include "GameObject/GameObject.h"
#include "GameObject/Component/Render/MeshRendererComponent.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "Graphics/Model/Model.h"
#include "Graphics/Material/MaterialInstance.h"
#include "GameObject/Component/Transform/ITransformSource.h"
#include "Utility/Logger/Logger.h"

#include <memory>

namespace CoreEngine
{
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

        Editor::EditorCommandStack::Get().Push(
            std::make_unique<Editor::FunctionCommand>(
                record.objectName + " の移動",
                [this, record] {
                    ApplyTransform(record.objectName, record.translateBefore,
                        record.rotateBefore, record.scaleBefore, record.activeBefore);
                },
                [this, record] {
                    ApplyTransform(record.objectName, record.translateAfter,
                        record.rotateAfter, record.scaleAfter, record.activeAfter);
                }));
    }

    // ===== スポーン操作の記録 =====

    void UndoRedoHistory::Push(const ObjectSpawnRecord& record)
    {
        Editor::EditorCommandStack::Get().Push(
            std::make_unique<Editor::FunctionCommand>(
                record.objectName + " の生成",
                [this, record] { DestroySpawned(record); },
                [this, record] { RespawnObject(record); }));
    }

    // ===== Undo / Redo =====

    bool UndoRedoHistory::Undo(GameObjectManager* manager)
    {
        if (manager) { manager_ = manager; }
        return Editor::EditorCommandStack::Get().Undo();
    }

    bool UndoRedoHistory::Redo(GameObjectManager* manager)
    {
        if (manager) { manager_ = manager; }
        return Editor::EditorCommandStack::Get().Redo();
    }

    bool UndoRedoHistory::CanUndo() const
    {
        return Editor::EditorCommandStack::Get().CanUndo();
    }

    bool UndoRedoHistory::CanRedo() const
    {
        return Editor::EditorCommandStack::Get().CanRedo();
    }

    int UndoRedoHistory::GetUndoCount() const
    {
        return static_cast<int>(Editor::EditorCommandStack::Get().GetUndoCount());
    }

    int UndoRedoHistory::GetRedoCount() const
    {
        return static_cast<int>(Editor::EditorCommandStack::Get().GetRedoCount());
    }

    void UndoRedoHistory::Clear()
    {
        Editor::EditorCommandStack::Get().Clear();
    }

    // ===== 実際の適用 =====

    void UndoRedoHistory::ApplyTransform(const std::string& name,
                                         const Vector3& translate, const Vector3& rotate,
                                         const Vector3& scale, bool active) const
    {
        if (!manager_) return;

        for (const auto& obj : manager_->GetAllObjects()) {
            if (obj && obj->GetName() == name) {
                if (auto* src = obj->GetComponent<ITransformSource>()) {
                    src->Translate() = translate;
                    src->Rotate() = rotate;
                    src->Scale() = scale;
                }
                obj->SetActive(active);
                break;
            }
        }
    }

    void UndoRedoHistory::DestroySpawned(const ObjectSpawnRecord& record) const
    {
        if (!manager_) return;

        // 削除前コールバック（ObjectSelector の選択解除など）
        if (onBeforeDestroy_) {
            onBeforeDestroy_(record.objectName);
        }
        manager_->DestroyByName(record.objectName);
        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System,
            "Undo: オブジェクトを削除しました: {}", record.objectName);
    }

    void UndoRedoHistory::RespawnObject(const ObjectSpawnRecord& record) const
    {
        if (!manager_) return;

        // トランスフォームとモデルファイルのメッシュ描画を持つ素のオブジェクトとして作り直す
        auto newObj = std::make_unique<GameObject>();
        newObj->SetName(record.objectName);
        GameObject* raw = manager_->AddObject(std::move(newObj));
        if (!raw) return;

        raw->AddComponent<TransformComponent>();
        auto* mesh = raw->AddComponent<MeshRendererComponent>(record.modelPath);

        if (Model* model = mesh ? mesh->GetModel() : nullptr) {
            model->ForEachMaterial([](MaterialInstance* material) {
                material->SetLightingEnabled(true);
                material->SetNormalMapEnabled(false);
            });
        }
        if (auto* src = raw->GetComponent<ITransformSource>()) {
            src->Translate() = record.translate;
            src->Rotate()    = record.rotate;
            src->Scale()     = record.scale;
        }
        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System,
            "Redo: オブジェクトを再生成しました: {}", record.objectName);
    }
}

#endif // USE_IMGUI
