#include "pch.h"
#include "Script/ScriptComponent.h"

#include "GameObject/GameObject.h"
#include "Reflection/PropertyValue.h"
#include "Script/Binding/GameObjectBinding.h"
#include "Script/ScriptHost.h"
#include "Utility/Logger/Logger.h"

#include <angelscript.h>
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
#ifdef USE_IMGUI
        , displayName_(type.GetDisplayName())
#endif
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

    void ScriptComponent::LateUpdate()
    {
        Invoke(ScriptComponentType::Method::LateUpdate);
    }

    void ScriptComponent::OnDestroy()
    {
        Invoke(ScriptComponentType::Method::OnDestroy);
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
        CopyValue(property.type, in, address);
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
        asIScriptFunction* const function = type_->GetMethod(method);
        if (!function) {
            return;
        }

        const bool finished = host_->CallMethod(function, object_,
            [this, method]() { return DescribeMethod(method); });
        if (!finished) {
            SetEnabled(false);
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Script,
                "{} が止まったので、このコンポーネントを無効にしました", DescribeMethod(method));
        }
    }
}
