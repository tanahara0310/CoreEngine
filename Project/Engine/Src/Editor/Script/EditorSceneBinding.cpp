#include "pch.h"
#include "Editor/Script/EditorScriptBinding.h"

#ifdef CORE_EDITOR

#include "Editor/Command/EditorCommandStack.h"
#include "Editor/Scene/EditorSceneAccess.h"
#include "Editor/Scene/ObjectEditing.h"
#include "Editor/Scene/SceneDebugEditor.h"
#include "EngineSystem/PlaybackState.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "Math/Vector/Vector3.h"
#include "Script/Binding/BindingRegistrar.h"
#include "Script/Binding/GameObjectBinding.h"

#include <angelscript.h>
#include <scriptarray/scriptarray.h>

#include <optional>
#include <string>

namespace CoreEngine::Editor::ScriptBinding
{
    namespace
    {
        using Script::ScriptGameObject;

        /// スクリプトが開いている Undo のまとまりの数
        int g_undoGroupDepth = 0;

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
    }

    CallScope::~CallScope()
    {
        for (; g_undoGroupDepth > 0; --g_undoGroupDepth) {
            EditorCommandStack::Get().EndBatch();
        }
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

        r.Namespace("");
        return r.Succeeded();
    }
}

#endif // CORE_EDITOR
