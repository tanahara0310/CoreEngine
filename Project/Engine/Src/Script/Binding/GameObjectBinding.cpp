#include "pch.h"
#include "Script/Binding/GameObjectBinding.h"

#include "GameObject/Component/Transform/TransformComponent.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "Reflection/PropertyDescriptor.h"
#include "Scene/PrefabSystem.h"
#include "Script/Binding/BindingRegistrar.h"
#include "Script/ScriptComponent.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine::Script
{
    // ---------------------------------------------------------------- ScriptTransform

    void ScriptTransform::AddRef() const
    {
        owner_.AddRef();
    }

    void ScriptTransform::Release() const
    {
        owner_.Release();
    }

    TransformComponent* ScriptTransform::ResolveOrWarn(const char* action) const
    {
        const GameObject* const object = owner_.Resolve();
        TransformComponent* const transform = object ? object->GetComponent<TransformComponent>() : nullptr;
        if (!transform && !warned_) {
            warned_ = true;
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Script,
                "{} の Transform で{}をしようとしましたが、GameObject が無いか Transform が付いていません",
                object ? object->GetName() : std::string("（実体の無い GameObject）"), action);
        }
        return transform;
    }

    bool ScriptTransform::Exists() const
    {
        const GameObject* const object = owner_.Resolve();
        return object && object->GetComponent<TransformComponent>() != nullptr;
    }

    Vector3 ScriptTransform::GetPosition() const
    {
        const TransformComponent* const transform = ResolveOrWarn("位置の読み取り");
        return transform ? transform->Get().translate : Vector3{};
    }

    void ScriptTransform::SetPosition(const Vector3& position)
    {
        if (TransformComponent* const transform = ResolveOrWarn("位置の変更")) {
            transform->Get().translate = position;
        }
    }

    Vector3 ScriptTransform::GetRotation() const
    {
        const TransformComponent* const transform = ResolveOrWarn("回転の読み取り");
        return transform ? transform->Get().rotate : Vector3{};
    }

    void ScriptTransform::SetRotation(const Vector3& rotation)
    {
        if (TransformComponent* const transform = ResolveOrWarn("回転の変更")) {
            transform->Get().rotate = rotation;
        }
    }

    Vector3 ScriptTransform::GetScale() const
    {
        const TransformComponent* const transform = ResolveOrWarn("拡大の読み取り");
        return transform ? transform->Get().scale : Vector3{ 1.0f, 1.0f, 1.0f };
    }

    void ScriptTransform::SetScale(const Vector3& scale)
    {
        if (TransformComponent* const transform = ResolveOrWarn("拡大の変更")) {
            transform->Get().scale = scale;
        }
    }

    Vector3 ScriptTransform::GetWorldPosition() const
    {
        const TransformComponent* const transform = ResolveOrWarn("ワールド位置の読み取り");
        return transform ? transform->GetWorldPosition() : Vector3{};
    }

    void ScriptTransform::SetParent(ScriptTransform* parent)
    {
        TransformComponent* const transform = ResolveOrWarn("親の変更");
        if (!transform) {
            return;
        }
        if (!parent) {
            transform->Get().SetParent(nullptr);
            return;
        }
        TransformComponent* const parentTransform = parent->ResolveOrWarn("親としての指定");
        if (!parentTransform || parentTransform == transform) {
            return;
        }
        transform->Get().SetParent(&parentTransform->Get());
    }

    void ScriptTransform::UpdateMatrix()
    {
        if (TransformComponent* const transform = ResolveOrWarn("行列の作り直し")) {
            transform->Get().TransferMatrix();
        }
    }

    ScriptGameObject* ScriptTransform::GetGameObject() const
    {
        owner_.AddRef();
        return &owner_;
    }

    // ---------------------------------------------------------------- ScriptGameObject

    ScriptGameObject* ScriptGameObject::CreateForOwner(const IComponent& component)
    {
        auto* handle = new ScriptGameObject();
        handle->component_ = &component;
        return handle;
    }

    ScriptGameObject* ScriptGameObject::CreateForObject(const GameObject* object)
    {
        if (!object) {
            return nullptr;
        }
        auto* handle = new ScriptGameObject();
        handle->objectId_ = object->GetObjectId();
        if (const GameObjectManager* const manager = object->GetObjectManager()) {
            handle->manager_ = manager->GetLifetimeToken();
        }
        return handle;
    }

    void ScriptGameObject::AddRef() const
    {
        ++refCount_;
    }

    void ScriptGameObject::Release() const
    {
        if (--refCount_ == 0) {
            delete this;
        }
    }

    GameObject* ScriptGameObject::Resolve() const
    {
        if (component_) {
            return component_->GetOwner();
        }
        if (!objectId_.IsValid()) {
            return nullptr;
        }
        const std::shared_ptr<const GameObjectManager*> manager = manager_.lock();
        return (manager && *manager) ? (*manager)->FindObject(objectId_) : nullptr;
    }

    GameObject* ScriptGameObject::ResolveOrWarn(const char* action) const
    {
        GameObject* const object = Resolve();
        if (!object && !warned_) {
            warned_ = true;
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Script,
                "指す先が無くなった GameObject で{}をしようとしました", action);
        }
        return object;
    }

    std::string ScriptGameObject::GetName() const
    {
        const GameObject* const object = ResolveOrWarn("名前の読み取り");
        return object ? object->GetName() : std::string();
    }

    void ScriptGameObject::SetName(const std::string& name)
    {
        if (GameObject* const object = ResolveOrWarn("名前の変更")) {
            object->SetName(name);
        }
    }

    bool ScriptGameObject::IsActive() const
    {
        const GameObject* const object = ResolveOrWarn("有効かの読み取り");
        return object && object->IsActive();
    }

    void ScriptGameObject::SetActive(bool active)
    {
        if (GameObject* const object = ResolveOrWarn("有効の切り替え")) {
            object->SetActive(active);
        }
    }

    bool ScriptGameObject::IsAlive() const
    {
        const GameObject* const object = Resolve();
        return object && !object->IsMarkedForDestroy();
    }

    void ScriptGameObject::Destroy()
    {
        if (GameObject* const object = ResolveOrWarn("破棄の予約")) {
            object->Destroy();
        }
    }

    bool ScriptGameObject::Equals(const ScriptGameObject& other) const
    {
        const GameObject* const object = Resolve();
        return object && object == other.Resolve();
    }

    ScriptTransform* ScriptGameObject::GetTransform()
    {
        AddRef();
        return &transform_;
    }

    ScriptGameObject* ScriptGameObject::FindObject(const std::string& name) const
    {
        const GameObject* const self = ResolveOrWarn("オブジェクトの検索");
        const GameObjectManager* const manager = self ? self->GetObjectManager() : nullptr;
        if (!manager) {
            return nullptr;
        }
        for (const auto& candidate : manager->GetAllObjects()) {
            if (candidate && !candidate->IsMarkedForDestroy() && candidate->GetName() == name) {
                return CreateForObject(candidate.get());
            }
        }
        return nullptr;
    }

    ScriptGameObject* ScriptGameObject::InstantiatePrefab(const std::string& prefabPath, const std::string& name) const
    {
        const GameObject* const self = ResolveOrWarn("プレハブからの生成");
        GameObjectManager* const manager = self ? self->GetObjectManager() : nullptr;
        if (!manager) {
            return nullptr;
        }
        const GameObject* const object =
            PrefabSystem::Instantiate(*manager, Reflection::AssetRefValue{ std::string(), prefabPath }, name);
        if (!object) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Script,
                "プレハブ {} を読めないので、{} を作れませんでした", prefabPath, name);
            return nullptr;
        }
        return CreateForObject(object);
    }

    bool ScriptGameObject::GetComponent(void* reference, int typeId) const
    {
        asIScriptContext* const context = asGetActiveContext();
        const bool scriptHandle = (typeId & asTYPEID_OBJHANDLE) != 0 && (typeId & asTYPEID_SCRIPTOBJECT) != 0;
        if (!reference || !scriptHandle) {
            if (context) {
                context->SetException("GetComponent には、スクリプトのクラスのハンドルを @ を付けて渡します（例: owner.GetComponent(@found)）");
            }
            return false;
        }

        const GameObject* const object = ResolveOrWarn("コンポーネントの検索");
        const asITypeInfo* const wanted = context
            ? context->GetEngine()->GetTypeInfoById(typeId & ~(asTYPEID_OBJHANDLE | asTYPEID_HANDLETOCONST))
            : nullptr;
        if (!object || !wanted) {
            return false;
        }
        for (const auto& component : object->GetAllComponents()) {
            const auto* const script = dynamic_cast<const ScriptComponent*>(component.get());
            asIScriptObject* const instance = script ? script->GetScriptObject() : nullptr;
            if (!instance || !instance->GetObjectType()->DerivesFrom(wanted)) {
                continue;
            }
            instance->AddRef();
            *static_cast<asIScriptObject**>(reference) = instance;
            return true;
        }
        return false;
    }

    // ---------------------------------------------------------------- 登録

    bool RegisterGameObjectBinding(asIScriptEngine* engine)
    {
        if (!engine) {
            return false;
        }
        BindingRegistrar r(engine);
        r.ReferenceType("GameObject", asOBJ_REF);
        r.ReferenceType("Transform", asOBJ_REF);

        r.Behaviour("GameObject", asBEHAVE_ADDREF, "void f()", asMETHOD(ScriptGameObject, AddRef), asCALL_THISCALL);
        r.Behaviour("GameObject", asBEHAVE_RELEASE, "void f()", asMETHOD(ScriptGameObject, Release), asCALL_THISCALL);
        r.Method("GameObject", "string get_name() const property", asMETHOD(ScriptGameObject, GetName), asCALL_THISCALL);
        r.Method("GameObject", "void set_name(const string &in) property", asMETHOD(ScriptGameObject, SetName), asCALL_THISCALL);
        r.Method("GameObject", "bool get_active() const property", asMETHOD(ScriptGameObject, IsActive), asCALL_THISCALL);
        r.Method("GameObject", "void set_active(bool) property", asMETHOD(ScriptGameObject, SetActive), asCALL_THISCALL);
        r.Method("GameObject", "bool get_isAlive() const property", asMETHOD(ScriptGameObject, IsAlive), asCALL_THISCALL);
        r.Method("GameObject", "void Destroy()", asMETHOD(ScriptGameObject, Destroy), asCALL_THISCALL);
        r.Method("GameObject", "bool opEquals(const GameObject &in) const", asMETHOD(ScriptGameObject, Equals), asCALL_THISCALL);
        r.Method("GameObject", "Transform@ get_transform() property", asMETHOD(ScriptGameObject, GetTransform), asCALL_THISCALL);
        r.Method("GameObject", "GameObject@ FindObject(const string &in name) const", asMETHOD(ScriptGameObject, FindObject), asCALL_THISCALL);
        r.Method("GameObject", "bool GetComponent(?&out component) const", asMETHOD(ScriptGameObject, GetComponent), asCALL_THISCALL);
        r.Method("GameObject", "GameObject@ InstantiatePrefab(const string &in prefabPath, const string &in name) const",
            asMETHOD(ScriptGameObject, InstantiatePrefab), asCALL_THISCALL);

        r.Behaviour("Transform", asBEHAVE_ADDREF, "void f()", asMETHOD(ScriptTransform, AddRef), asCALL_THISCALL);
        r.Behaviour("Transform", asBEHAVE_RELEASE, "void f()", asMETHOD(ScriptTransform, Release), asCALL_THISCALL);
        r.Method("Transform", "bool get_exists() const property", asMETHOD(ScriptTransform, Exists), asCALL_THISCALL);
        r.Method("Transform", "Vector3 get_position() const property", asMETHOD(ScriptTransform, GetPosition), asCALL_THISCALL);
        r.Method("Transform", "void set_position(const Vector3 &in) property", asMETHOD(ScriptTransform, SetPosition), asCALL_THISCALL);
        r.Method("Transform", "Vector3 get_rotation() const property", asMETHOD(ScriptTransform, GetRotation), asCALL_THISCALL);
        r.Method("Transform", "void set_rotation(const Vector3 &in) property", asMETHOD(ScriptTransform, SetRotation), asCALL_THISCALL);
        r.Method("Transform", "Vector3 get_scale() const property", asMETHOD(ScriptTransform, GetScale), asCALL_THISCALL);
        r.Method("Transform", "void set_scale(const Vector3 &in) property", asMETHOD(ScriptTransform, SetScale), asCALL_THISCALL);
        r.Method("Transform", "Vector3 get_worldPosition() const property", asMETHOD(ScriptTransform, GetWorldPosition), asCALL_THISCALL);
        r.Method("Transform", "void SetParent(Transform@+ parent)", asMETHOD(ScriptTransform, SetParent), asCALL_THISCALL);
        r.Method("Transform", "void UpdateMatrix()", asMETHOD(ScriptTransform, UpdateMatrix), asCALL_THISCALL);
        r.Method("Transform", "GameObject@ get_gameObject() const property", asMETHOD(ScriptTransform, GetGameObject), asCALL_THISCALL);
        return r.Succeeded();
    }
}
