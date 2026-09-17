#include "pch.h"
#ifdef USE_IMGUI

#include "UndoRedoHistory.h"
#include "Editor/Command/EditorCommandStack.h"
#include "Editor/Scene/EditorSceneAccess.h"
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
    namespace
    {
        /// @brief 今のシーンから ID で引いたオブジェクトへ値を書き戻す
        void ApplyTransform(ObjectId id, const Vector3& translate, const Vector3& rotate,
                            const Vector3& scale, bool active)
        {
            GameObject* const object = Editor::SceneAccess::FindObject(id);
            if (!object) {
                return;
            }
            if (auto* src = object->GetComponent<ITransformSource>()) {
                src->Translate() = translate;
                src->Rotate() = rotate;
                src->Scale() = scale;
            }
            object->SetActive(active);
        }

        /// @brief スポーンを取り消す（オブジェクトを削除する）
        void DestroySpawned(const ObjectSpawnRecord& record)
        {
            GameObjectManager* const manager = Editor::SceneAccess::Objects();
            GameObject* const object = manager ? manager->FindObject(record.objectId) : nullptr;
            if (!object) {
                return;
            }

            Editor::SceneAccess::Deselect(*object);
            object->Destroy();
            manager->InvalidateReferences();
            Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System,
                "Undo: オブジェクトを削除しました: {}", record.objectName);
        }

        /// @brief スポーンをやり直す（同じ ID と保存キーでオブジェクトを作り直す）
        void RespawnObject(const ObjectSpawnRecord& record)
        {
            GameObjectManager* const manager = Editor::SceneAccess::Objects();
            if (!manager) {
                return;
            }

            // トランスフォームとモデルファイルのメッシュ描画を持つ素のオブジェクトとして作り直す
            auto newObj = std::make_unique<GameObject>();
            newObj->SetName(record.objectName);
            GameObject* raw = manager->AddObject(std::move(newObj));
            if (!raw) {
                return;
            }
            raw->SetSerializeKey(record.serializeKey);
            manager->AssignObjectId(*raw, record.objectId);

            // エディタが作るオブジェクトのコンポーネントとして付ける
            ComponentHost::DataAttachScope dataScope(*raw);
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
            manager->InvalidateReferences();
            Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System,
                "Redo: オブジェクトを再生成しました: {}", record.objectName);
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

        Editor::EditorCommandStack::Get().Push(
            std::make_unique<Editor::FunctionCommand>(
                record.objectName + " の移動",
                [record] {
                    ApplyTransform(record.objectId, record.translateBefore,
                        record.rotateBefore, record.scaleBefore, record.activeBefore);
                },
                [record] {
                    ApplyTransform(record.objectId, record.translateAfter,
                        record.rotateAfter, record.scaleAfter, record.activeAfter);
                },
                true, true));
    }

    // ===== スポーン操作の記録 =====

    void UndoRedoHistory::Push(const ObjectSpawnRecord& record)
    {
        Editor::EditorCommandStack::Get().Push(
            std::make_unique<Editor::FunctionCommand>(
                record.objectName + " の生成",
                [record] { DestroySpawned(record); },
                [record] { RespawnObject(record); },
                true, true));
    }

    // ===== Undo / Redo =====

    bool UndoRedoHistory::Undo()
    {
        return Editor::EditorCommandStack::Get().Undo();
    }

    bool UndoRedoHistory::Redo()
    {
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
}

#endif // USE_IMGUI
