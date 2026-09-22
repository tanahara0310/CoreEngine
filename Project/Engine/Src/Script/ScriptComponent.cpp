#include "pch.h"
#include "Script/ScriptComponent.h"

#ifdef CORE_EDITOR
#include "Editor/Command/EditorCommandStack.h"
#endif
#include "GameObject/Component/Core/ObjectRef.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "Graphics/Asset/AssetDatabase.h"
#include "Graphics/Asset/AssetRef.h"
#include "Reflection/PropertySerializer.h"
#include "Reflection/PropertyValue.h"
#include "Script/Binding/GameObjectBinding.h"
#include "Script/Binding/PhysicsBinding.h"
#include "Script/ScriptHost.h"
#include "Utility/Logger/Logger.h"

#include <angelscript.h>
#include <chrono>
#include <scriptarray/scriptarray.h>

namespace CoreEngine
{
    namespace
    {
        /// @brief 型に合わせて値を 1 つ写す（参照と配列の型は写さない）
        void CopyValue(Reflection::PropertyType type, const void* from, void* to)
        {
            switch (type) {
            case Reflection::PropertyType::Bool:
                *static_cast<bool*>(to) = *static_cast<const bool*>(from);
                break;
            case Reflection::PropertyType::Int:
                *static_cast<int*>(to) = *static_cast<const int*>(from);
                break;
            case Reflection::PropertyType::Float:
                *static_cast<float*>(to) = *static_cast<const float*>(from);
                break;
            case Reflection::PropertyType::String:
                *static_cast<std::string*>(to) = *static_cast<const std::string*>(from);
                break;
            case Reflection::PropertyType::Vector2:
                *static_cast<Vector2*>(to) = *static_cast<const Vector2*>(from);
                break;
            case Reflection::PropertyType::Vector3:
                *static_cast<Vector3*>(to) = *static_cast<const Vector3*>(from);
                break;
            case Reflection::PropertyType::Vector4:
            case Reflection::PropertyType::Color:
                *static_cast<Vector4*>(to) = *static_cast<const Vector4*>(from);
                break;
            default:
                break;
            }
        }

        /// @brief スクリプトの配列を記述子の配列の値へ写す
        void ReadArray(Reflection::PropertyType elementType, const CScriptArray& from, Reflection::ArrayValue& to)
        {
            to.elements.clear();
            to.elements.reserve(from.GetSize());
            for (asUINT i = 0; i < from.GetSize(); ++i) {
                Reflection::ArrayValue::Element element = Reflection::MakeArrayElement(elementType);
                if (void* const data = Reflection::ArrayElementData(elementType, element)) {
                    CopyValue(elementType, from.At(i), data);
                }
                to.elements.push_back(std::move(element));
            }
        }

        /// @brief 記述子の配列の値をスクリプトの配列へ写す（要素の数も合わせる）
        void WriteArray(Reflection::PropertyType elementType, const Reflection::ArrayValue& from, CScriptArray& to)
        {
            to.Resize(static_cast<asUINT>(from.elements.size()));
            for (asUINT i = 0; i < to.GetSize(); ++i) {
                if (const void* const data = Reflection::ArrayElementData(elementType, from.elements[i])) {
                    CopyValue(elementType, data, to.At(i));
                }
            }
        }
    }

    ScriptComponent::ScriptComponent(const ScriptComponentType& type)
        : typeName_(type.GetName())
        , type_(&type)
        , host_(&type.GetHost())
    {
        host_->RegisterComponent(this);
        object_ = host_->CreateObject(type);
        if (!object_) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Script,
                "スクリプトのクラス {} のオブジェクトを作れませんでした", typeName_);
            return;
        }
        BindOwnerHandle();
    }

    ScriptComponent::~ScriptComponent()
    {
        ReleaseOwnerHandle();
        if (object_) {
            object_->Release();
            object_ = nullptr;
        }
        if (host_) {
            host_->UnregisterComponent(this);
        }
    }

    const Reflection::TypeDescriptor* ScriptComponent::GetTypeDescriptor() const
    {
        return (type_ && object_) ? &type_->GetDescriptor() : nullptr;
    }

    void ScriptComponent::Awake()
    {
        Invoke(ScriptComponentType::Method::Awake);
    }

    void ScriptComponent::Start()
    {
        Invoke(ScriptComponentType::Method::Start);
    }

    void ScriptComponent::Update()
    {
        Invoke(ScriptComponentType::Method::Update);
    }

    void ScriptComponent::FixedUpdate()
    {
        Invoke(ScriptComponentType::Method::FixedUpdate);
    }

    void ScriptComponent::LateUpdate()
    {
        Invoke(ScriptComponentType::Method::LateUpdate);
    }

    void ScriptComponent::OnDestroy()
    {
        Invoke(ScriptComponentType::Method::OnDestroy);
    }

    void ScriptComponent::OnCollisionEnter(const CollisionInfo& info)
    {
        InvokeContact(ScriptComponentType::Method::OnCollisionEnter, info, false);
    }

    void ScriptComponent::OnCollisionStay(const CollisionInfo& info)
    {
        InvokeContact(ScriptComponentType::Method::OnCollisionStay, info, false);
    }

    void ScriptComponent::OnCollisionExit(const CollisionInfo& info)
    {
        InvokeContact(ScriptComponentType::Method::OnCollisionExit, info, false);
    }

    void ScriptComponent::OnTriggerEnter(const CollisionInfo& info)
    {
        InvokeContact(ScriptComponentType::Method::OnTriggerEnter, info, true);
    }

    void ScriptComponent::OnTriggerStay(const CollisionInfo& info)
    {
        InvokeContact(ScriptComponentType::Method::OnTriggerStay, info, true);
    }

    void ScriptComponent::OnTriggerExit(const CollisionInfo& info)
    {
        InvokeContact(ScriptComponentType::Method::OnTriggerExit, info, true);
    }

    void ScriptComponent::ReadProperty(const Reflection::PropertyDescriptor& property, void* out) const
    {
        if (!object_ || !out || property.index >= object_->GetPropertyCount()) {
            return;
        }
        const void* const address = object_->GetAddressOfProperty(property.index);
        if (!address) {
            return;
        }

        if (property.type == Reflection::PropertyType::Array) {
            ReadArray(property.elementType, *static_cast<const CScriptArray*>(address),
                *static_cast<Reflection::ArrayValue*>(out));
            return;
        }
        if (property.type == Reflection::PropertyType::ObjectRef) {
            const auto it = objectRefs_.find(property.index);
            *static_cast<Reflection::ObjectRefValue*>(out) =
                it != objectRefs_.end() ? it->second.value : Reflection::ObjectRefValue{};
            return;
        }
        if (property.type == Reflection::PropertyType::AssetRef) {
            ReadAssetRef(property, *static_cast<const std::string*>(address),
                *static_cast<Reflection::AssetRefValue*>(out));
            return;
        }
        CopyValue(property.type, address, out);
    }

    void ScriptComponent::WriteProperty(const Reflection::PropertyDescriptor& property, const void* in)
    {
        if (!object_ || !in || property.index >= object_->GetPropertyCount()) {
            return;
        }
        void* const address = object_->GetAddressOfProperty(property.index);
        if (!address) {
            return;
        }

        if (property.type == Reflection::PropertyType::Array) {
            WriteArray(property.elementType, *static_cast<const Reflection::ArrayValue*>(in),
                *static_cast<CScriptArray*>(address));
            return;
        }
        if (property.type == Reflection::PropertyType::ObjectRef) {
            ObjectRefSlot& slot = objectRefs_[property.index];
            slot.property = &property;
            slot.value = *static_cast<const Reflection::ObjectRefValue*>(in);
            ApplyObjectRef(property.index);
            return;
        }
        if (property.type == Reflection::PropertyType::AssetRef) {
            WriteAssetRef(property, *static_cast<const Reflection::AssetRefValue*>(in),
                *static_cast<std::string*>(address));
            return;
        }
        CopyValue(property.type, in, address);
    }

    void ScriptComponent::ReadAssetRef(const Reflection::PropertyDescriptor& property, const std::string& path,
        Reflection::AssetRefValue& out) const
    {
        // 最後に書き込んだパスのままなら、控えた GUID を使う
        if (const auto it = assetRefs_.find(property.index); it != assetRefs_.end() && it->second.path == path) {
            out = it->second;
            return;
        }
        // スクリプトがパスを書き換えたときは、そのパスから GUID を引き直す
        out.path = path;
        const AssetInfo* const info = path.empty() ? nullptr : FindAssetInfo(path, property.assetType);
        out.guid = info ? info->guid : std::string();
    }

    void ScriptComponent::WriteAssetRef(const Reflection::PropertyDescriptor& property,
        const Reflection::AssetRefValue& in, std::string& path)
    {
        // GUID で引ければ今のパスへ直す（ファイルを動かしても参照が切れない）
        Reflection::AssetRefValue resolved = in;
        if (const AssetInfo* const info = ResolveAssetRef(in)) {
            resolved.guid = info->guid;
            resolved.path = ToAssetPath(*info);
        }
        path = resolved.path;
        assetRefs_[property.index] = std::move(resolved);
    }

    void ScriptComponent::ApplyObjectRefs()
    {
        const GameObject* const owner = GetOwner();
        const GameObjectManager* const manager = owner ? owner->GetObjectManager() : nullptr;
        if (!manager) {
            return;
        }
        const std::uint64_t epoch = manager->GetReferenceEpoch();
        for (auto& [index, slot] : objectRefs_) {
            if (slot.appliedEpoch != epoch) {
                ApplyObjectRef(index);
            }
        }
    }

    void ScriptComponent::ApplyObjectRef(std::uint32_t index)
    {
        const auto it = objectRefs_.find(index);
        const GameObject* const owner = GetOwner();
        const GameObjectManager* const manager = owner ? owner->GetObjectManager() : nullptr;
        if (it == objectRefs_.end() || !object_ || !host_ || !manager) {
            return;
        }
        ObjectRefSlot& slot = it->second;

        // 繋ぎ先が消えた・見つからないときは null を入れる
        void* handle = nullptr;
        const GameObject* const target =
            slot.value.objectId.IsValid() ? manager->FindObject(slot.value.objectId) : nullptr;
        if (target && !target->IsMarkedForDestroy() && slot.property) {
            if (object_->GetPropertyTypeId(index) == host_->GetGameObjectHandleTypeId()) {
                handle = Script::ScriptGameObject::CreateForObject(target);
            } else if (auto* const component = dynamic_cast<ScriptComponent*>(
                           FindReferencedComponent(*target, *slot.property, slot.value.componentType))) {
                if (component->object_) {
                    component->object_->AddRef();
                    handle = component->object_;
                }
            }
        }
        StoreHandle(index, handle);
        slot.appliedEpoch = manager->GetReferenceEpoch();
    }

    void ScriptComponent::StoreHandle(std::uint32_t index, void* handle)
    {
        asIScriptEngine* const engine = object_->GetEngine();
        asITypeInfo* const type = engine->GetTypeInfoById(object_->GetPropertyTypeId(index));
        auto** const slot = static_cast<void**>(object_->GetAddressOfProperty(index));
        if (!slot || !type) {
            // 入れる先が無いときは、引き取った参照をそのまま手放す
            if (handle && type) {
                engine->ReleaseScriptObject(handle, type);
            }
            return;
        }
        if (*slot) {
            engine->ReleaseScriptObject(*slot, type);
        }
        *slot = handle;
    }

    void ScriptComponent::BindOwnerHandle()
    {
        const int index = type_ ? type_->GetOwnerPropertyIndex() : -1;
        if (!object_ || index < 0) {
            return;
        }
        auto** const slot = static_cast<Script::ScriptGameObject**>(
            object_->GetAddressOfProperty(static_cast<asUINT>(index)));
        if (!slot) {
            return;
        }
        ReleaseOwnerHandle();
        ownerHandle_ = Script::ScriptGameObject::CreateForOwner(*this);
        if (*slot) {
            (*slot)->Release();
        }
        ownerHandle_->AddRef();
        *slot = ownerHandle_;
    }

    void ScriptComponent::ReleaseOwnerHandle()
    {
        if (!ownerHandle_) {
            return;
        }
        ownerHandle_->DetachComponent();
        ownerHandle_->Release();
        ownerHandle_ = nullptr;
    }

    void ScriptComponent::ReleaseScriptObject()
    {
        ReleaseOwnerHandle();
        if (object_) {
            object_->Release();
            object_ = nullptr;
        }
        type_ = nullptr;
        host_ = nullptr;
    }

    void ScriptComponent::PrepareForReload()
    {
#ifdef CORE_EDITOR
        // 型の記述子を握っている Undo の操作は、型ごと作り直すので履歴から外す
        Editor::EditorCommandStack::Get().RemoveCommandsReferencing(this);
#endif

        if (type_ && object_) {
            savedParameters_ = json::object();
            Reflection::PropertySerializer::Save(type_->GetDescriptor(), this, savedParameters_);
        }

        ReleaseOwnerHandle();
        if (object_) {
            object_->Release();
            object_ = nullptr;
        }
        type_ = nullptr;
        objectRefs_.clear();
        assetRefs_.clear();
    }

    bool ScriptComponent::RebindType(const ScriptComponentType& type)
    {
        if (!host_) {
            return false;
        }
        typeName_ = type.GetName();
        type_ = &type;
        object_ = host_->CreateObject(type);
        if (!object_) {
            type_ = nullptr;
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Script,
                "読み直した後に、スクリプトのクラス {} のオブジェクトを作れませんでした", typeName_);
            return false;
        }

        BindOwnerHandle();
        if (savedParameters_.is_object()) {
            Reflection::PropertySerializer::Load(type.GetDescriptor(), this, savedParameters_);
            savedParameters_ = json();
        }
        return true;
    }

    void ScriptComponent::NotifyScriptReloaded()
    {
        Invoke(ScriptComponentType::Method::OnScriptReloaded);
    }

    std::string ScriptComponent::DescribeMethod(ScriptComponentType::Method method) const
    {
        const GameObject* const owner = GetOwner();
        return (owner ? owner->GetName() : std::string("（持ち主なし）")) + " の " + typeName_ + "::" +
            ScriptComponentType::GetMethodName(method);
    }

    void ScriptComponent::Invoke(ScriptComponentType::Method method)
    {
        if (!type_ || !host_ || !object_) {
            return;
        }
        if (!objectRefs_.empty()) {
            ApplyObjectRefs();
        }
        asIScriptFunction* const function = type_->GetMethod(method);
        if (!function) {
            return;
        }

        // 毎フレーム呼ばれる関数だけ、型ごとの実行時間として数える
        const bool perFrame = method == ScriptComponentType::Method::Update
            || method == ScriptComponentType::Method::LateUpdate;
        const auto started = perFrame ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};

        const bool finished = host_->CallMethod(function, object_,
            [this, method]() { return DescribeMethod(method); });

        if (perFrame) {
            const std::chrono::duration<double, std::milli> elapsed = std::chrono::steady_clock::now() - started;
            type_->AddFrameCost(elapsed.count(), method == ScriptComponentType::Method::Update);
        }
        if (!finished) {
            SetEnabled(false);
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Script,
                "{} が止まったので、このコンポーネントを無効にしました", DescribeMethod(method));
        }
    }

    void ScriptComponent::InvokeContact(ScriptComponentType::Method method, const CollisionInfo& info, bool trigger)
    {
        if (!type_ || !host_ || !object_) {
            return;
        }
        asIScriptFunction* const function = type_->GetMethod(method);
        if (!function) {
            return;
        }
        if (!objectRefs_.empty()) {
            ApplyObjectRefs();
        }

        const auto started = std::chrono::steady_clock::now();
        Script::ScriptCollision* const collision = Script::ScriptCollision::Create(info, trigger);
        const bool finished = host_->CallMethod(function, object_,
            [collision](asIScriptContext* context) { return context->SetArgObject(0, collision); },
            [this, method]() { return DescribeMethod(method); });
        collision->Release();

        // 触れている間は毎フレーム呼ばれるので、型ごとの実行時間に数える（実体の数には数えない）
        const std::chrono::duration<double, std::milli> elapsed = std::chrono::steady_clock::now() - started;
        type_->AddFrameCost(elapsed.count(), false);

        if (!finished) {
            SetEnabled(false);
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Script,
                "{} が止まったので、このコンポーネントを無効にしました", DescribeMethod(method));
        }
    }
}
