#include "pch.h"
#include "Editor/Script/EditorScriptBinding.h"

#ifdef CORE_EDITOR

#include "Editor/Command/EditorCommandStack.h"
#include "Editor/Scene/EditorSceneAccess.h"
#include "Editor/Scene/ObjectEditing.h"
#include "Editor/Scene/SceneDebugEditor.h"
#include "EngineSystem/PlaybackState.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "Graphics/Asset/AssetDatabase.h"
#include "Graphics/Asset/AssetInfo.h"
#include "Graphics/Asset/AssetRef.h"
#include "Graphics/Asset/AssetType.h"
#include "Math/Vector/Vector3.h"
#include "Script/Binding/BindingRegistrar.h"
#include "Script/Binding/GameObjectBinding.h"

#include <angelscript.h>
#include <imgui.h>
#include <ImGuizmo.h>
#include <scriptarray/scriptarray.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace CoreEngine::Editor::ScriptBinding
{
    namespace
    {
        using Script::ScriptGameObject;

        /// スクリプトが開いている Undo のまとまりの数
        int g_undoGroupDepth = 0;

        /// 入れ子になった CallScope の数
        int g_callDepth = 0;

        /// @brief RecordObject で控えた、変える前の状態
        struct RecordedObject
        {
            ObjectId id{};
            std::string label;
            json before;
        };

        /// 今の呼び出しで RecordObject したオブジェクト
        std::vector<RecordedObject> g_recorded;

        /// @brief Undo / Redo で書き戻す状態（積んだ後も、ドラッグの間は変えた後の状態を新しくする）
        struct RecordedStates
        {
            json before;
            json after;
        };

        /// @brief 最後に積んだ RecordObject の操作
        struct LastRecord
        {
            ObjectId id{};
            std::string label;
            std::size_t undoCount = 0;
            std::shared_ptr<RecordedStates> states;
        };

        LastRecord g_lastRecord;

        /// @brief 今のシーンのオブジェクトへ状態を書き戻す
        void RestoreObjectState(ObjectId id, const json& state)
        {
            GameObject* const object = SceneAccess::FindObject(id);
            if (!object) {
                return;
            }
            object->Deserialize(state);
            if (TransformComponent* const transform = object->GetComponent<TransformComponent>()) {
                transform->SyncWorldMatrix();
            }
        }

        /// @brief 控えたオブジェクトのうち変わったものを Undo に積む
        /// @details 同じオブジェクトを同じ名前で控え、ドラッグが続いている間は、直前に積んだ操作の「変えた後」を新しくする。
        void FlushRecordedObjects()
        {
            if (g_recorded.empty()) {
                return;
            }
            std::vector<RecordedObject> recorded = std::move(g_recorded);
            g_recorded.clear();

            struct Change
            {
                RecordedObject record;
                json after;
            };
            std::vector<Change> changes;
            for (RecordedObject& record : recorded) {
                const GameObject* const object = SceneAccess::FindObject(record.id);
                if (!object) {
                    continue;
                }
                json after = object->Serialize();
                if (after != record.before) {
                    changes.push_back(Change{ std::move(record), std::move(after) });
                }
            }
            if (changes.empty()) {
                return;
            }

            EditorCommandStack& stack = EditorCommandStack::Get();
            // 部品かシーンビューのハンドルをドラッグしている間は、直前の操作の「変えた後」を新しくして 1 つにまとめる
            const bool dragging = ImGui::IsAnyItemActive() || ImGuizmo::IsUsingAny();
            if (changes.size() == 1 && g_lastRecord.states && dragging
                && g_lastRecord.id == changes.front().record.id && g_lastRecord.label == changes.front().record.label
                && g_lastRecord.undoCount == stack.GetUndoCount() && stack.PeekUndoLabel() == g_lastRecord.label) {
                g_lastRecord.states->after = std::move(changes.front().after);
                return;
            }

            LastRecord last;
            {
                BatchScope batch(changes.front().record.label);
                for (Change& change : changes) {
                    auto states = std::make_shared<RecordedStates>(RecordedStates{ std::move(change.record.before), std::move(change.after) });
                    const ObjectId id = change.record.id;
                    stack.Push(std::make_unique<FunctionCommand>(
                        change.record.label,
                        [id, states] { RestoreObjectState(id, states->before); },
                        [id, states] { RestoreObjectState(id, states->after); },
                        true, true));
                    last = LastRecord{ id, change.record.label, 0, states };
                }
            }
            last.undoCount = stack.GetUndoCount();
            g_lastRecord = changes.size() == 1 ? std::move(last) : LastRecord{};
        }

        void ThrowScriptException(const std::string& message)
        {
            if (asIScriptContext* const context = asGetActiveContext()) {
                context->SetException(message.c_str());
            }
        }

        /// @brief 編集中のシーンを操作する準備（できなければスクリプトの例外にする）
        /// @param action 例外の文に入れる操作の名前（「オブジェクトの作成」など）
        std::optional<ObjectEditing::Context> EditingContext(const char* action)
        {
            GameObjectManager* const manager = SceneAccess::Objects();
            if (!manager) {
                ThrowScriptException(std::string("シーンが開いていないので、") + action + "はできません");
                return std::nullopt;
            }
            if (const SceneDebugEditor* const sceneEditor = SceneAccess::SceneEditor(); sceneEditor && sceneEditor->IsInPrefabMode()) {
                ThrowScriptException(std::string("プレハブを編集している間は、") + action + "はできません");
                return std::nullopt;
            }
            ObjectEditing::Context context;
            context.manager = manager;
            context.beforeDestroy = [](const GameObject& object) { SceneAccess::Deselect(object); };
            return context;
        }

        /// @brief 要素の型を指定した空の配列を作る（スクリプトの中から呼ばれたときだけ作れる）
        CScriptArray* CreateArray(const char* declaration)
        {
            asIScriptContext* const context = asGetActiveContext();
            asIScriptEngine* const engine = context ? context->GetEngine() : nullptr;
            asITypeInfo* const type = engine ? engine->GetTypeInfoByDecl(declaration) : nullptr;
            return type ? CScriptArray::Create(type) : nullptr;
        }

        ScriptGameObject* CreateObjectAt(const std::string& name, const Vector3& position)
        {
            const std::optional<ObjectEditing::Context> context = EditingContext("オブジェクトの作成");
            if (!context) {
                return nullptr;
            }
            return ScriptGameObject::CreateForObject(ObjectEditing::CreateNamed(*context, name, position));
        }

        ScriptGameObject* CreateObject(const std::string& name)
        {
            return CreateObjectAt(name, Vector3{ 0.0f, 0.0f, 0.0f });
        }

        ScriptGameObject* InstantiatePrefab(const std::string& prefabPath, const std::string& name)
        {
            const std::optional<ObjectEditing::Context> context = EditingContext("プレハブからの作成");
            if (!context) {
                return nullptr;
            }
            return ScriptGameObject::CreateForObject(ObjectEditing::InstantiatePrefab(*context, prefabPath, name));
        }

        bool DestroyObject(ScriptGameObject* handle)
        {
            GameObject* const object = handle ? handle->Resolve() : nullptr;
            if (!object) {
                ThrowScriptException("Editor::DestroyObject に、指す先の無い GameObject が渡されました");
                return false;
            }
            const std::optional<ObjectEditing::Context> context = EditingContext("オブジェクトの削除");
            return context && ObjectEditing::DeleteWithDescendants(*context, *object);
        }

        ScriptGameObject* FindObject(const std::string& name)
        {
            const GameObjectManager* const manager = SceneAccess::Objects();
            const GameObject* const found = manager ? manager->FindObjectByName(name) : nullptr;
            return found ? ScriptGameObject::CreateForObject(found) : nullptr;
        }

        CScriptArray* GetObjects()
        {
            CScriptArray* const result = CreateArray("array<GameObject@>");
            const GameObjectManager* const manager = SceneAccess::Objects();
            if (!result || !manager) {
                return result;
            }
            for (const auto& object : manager->GetAllObjects()) {
                if (!object || object->IsMarkedForDestroy()) {
                    continue;
                }
                ScriptGameObject* handle = ScriptGameObject::CreateForObject(object.get());
                result->InsertLast(&handle);
                handle->Release();
            }
            return result;
        }

        ScriptGameObject* GetSelection()
        {
            const SceneDebugEditor* const sceneEditor = SceneAccess::SceneEditor();
            const GameObject* const selected = sceneEditor ? sceneEditor->GetSelectedObject() : nullptr;
            return selected ? ScriptGameObject::CreateForObject(selected) : nullptr;
        }

        void Select(ScriptGameObject* handle)
        {
            if (SceneDebugEditor* const sceneEditor = SceneAccess::SceneEditor()) {
                sceneEditor->SelectObject(handle ? handle->Resolve() : nullptr);
            }
        }

        void BeginUndoGroup(const std::string& name)
        {
            EditorCommandStack::Get().BeginBatch(name);
            ++g_undoGroupDepth;
        }

        void EndUndoGroup()
        {
            if (g_undoGroupDepth <= 0) {
                ThrowScriptException("Editor::EndUndoGroup が BeginUndoGroup より多く呼ばれました");
                return;
            }
            EditorCommandStack::Get().EndBatch();
            --g_undoGroupDepth;
        }

        bool IsPlaying()
        {
            return !PlaybackStateManager::GetInstance().IsEditing();
        }

        void RecordObject(ScriptGameObject* handle, const std::string& label)
        {
            const GameObject* const object = handle ? handle->Resolve() : nullptr;
            if (!object) {
                ThrowScriptException("Editor::RecordObject に、指す先の無い GameObject が渡されました");
                return;
            }
            if (g_callDepth <= 0) {
                ThrowScriptException("Editor::RecordObject は、EditorWindow とメニューの関数の中で呼びます");
                return;
            }
            const ObjectId id = object->GetObjectId();
            const bool recorded = std::any_of(g_recorded.begin(), g_recorded.end(),
                [id](const RecordedObject& record) { return record.id == id; });
            if (!recorded) {
                g_recorded.push_back(RecordedObject{ id, label.empty() ? object->GetName() + " を変更" : label, object->Serialize() });
            }
        }

        CScriptArray* FindAssets(const std::string& type, const std::string& folder)
        {
            std::vector<AssetType> types;
            if (type.empty()) {
                for (int value = static_cast<int>(AssetType::Texture); value <= static_cast<int>(AssetType::PhysicsMaterial); ++value) {
                    types.push_back(static_cast<AssetType>(value));
                }
            } else {
                const AssetType assetType = StringToAssetType(type);
                if (assetType == AssetType::Unknown) {
                    ThrowScriptException("Editor::FindAssets の種類 " + type + " はありません（Texture・Model・Audio・Prefab・Json・Csv など）");
                    return nullptr;
                }
                types.push_back(assetType);
            }

            std::vector<std::string> paths;
            for (const AssetType assetType : types) {
                for (const AssetInfo* const info : AssetDatabase::GetInstance().GetAssetsOfType(assetType)) {
                    std::string path = ToAssetPath(*info);
                    if (folder.empty() || path.starts_with(folder)) {
                        paths.push_back(std::move(path));
                    }
                }
            }
            std::sort(paths.begin(), paths.end());

            CScriptArray* const result = CreateArray("array<string>");
            if (!result) {
                return nullptr;
            }
            for (std::string& path : paths) {
                result->InsertLast(&path);
            }
            return result;
        }
    }

    CallScope::CallScope()
    {
        ++g_callDepth;
    }

    CallScope::~CallScope()
    {
        if (--g_callDepth > 0) {
            return;
        }
        g_callDepth = 0;
        FlushRecordedObjects();
        for (; g_undoGroupDepth > 0; --g_undoGroupDepth) {
            EditorCommandStack::Get().EndBatch();
        }
    }

    bool IsInEditorCall()
    {
        return g_callDepth > 0;
    }

    bool RegisterEditorScene(asIScriptEngine* engine)
    {
        Script::BindingRegistrar r(engine);
        r.Namespace("Editor");

        r.Function("GameObject@ CreateObject(const string &in name)", asFUNCTION(CreateObject));
        r.Function("GameObject@ CreateObject(const string &in name, const Vector3 &in position)", asFUNCTION(CreateObjectAt));
        r.Function("GameObject@ InstantiatePrefab(const string &in prefabPath, const string &in name)", asFUNCTION(InstantiatePrefab));
        r.Function("bool DestroyObject(GameObject@+ object)", asFUNCTION(DestroyObject));
        r.Function("GameObject@ FindObject(const string &in name)", asFUNCTION(FindObject));
        r.Function("array<GameObject@>@ GetObjects()", asFUNCTION(GetObjects));
        r.Function("GameObject@ GetSelection()", asFUNCTION(GetSelection));
        r.Function("void Select(GameObject@+ object)", asFUNCTION(Select));
        r.Function("void BeginUndoGroup(const string &in name)", asFUNCTION(BeginUndoGroup));
        r.Function("void EndUndoGroup()", asFUNCTION(EndUndoGroup));
        r.Function("bool get_isPlaying() property", asFUNCTION(IsPlaying));
        r.Function("void RecordObject(GameObject@+ object, const string &in label = \"\")", asFUNCTION(RecordObject));
        r.Function("array<string>@ FindAssets(const string &in type = \"\", const string &in folder = \"\")", asFUNCTION(FindAssets));

        r.Namespace("");
        return r.Succeeded();
    }
}

#endif // CORE_EDITOR
