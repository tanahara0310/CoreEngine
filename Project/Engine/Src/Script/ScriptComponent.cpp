#include "pch.h"
#include "Script/ScriptComponent.h"

#include "GameObject/GameObject.h"
#include "Script/ScriptHost.h"
#include "Utility/Logger/Logger.h"

#include <angelscript.h>

namespace CoreEngine
{
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
        }
    }

    ScriptComponent::~ScriptComponent()
    {
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

        switch (property.type) {
        case Reflection::PropertyType::Bool:
            *static_cast<bool*>(out) = *static_cast<const bool*>(address);
            break;
        case Reflection::PropertyType::Int:
            *static_cast<int*>(out) = *static_cast<const int*>(address);
            break;
        case Reflection::PropertyType::Float:
            *static_cast<float*>(out) = *static_cast<const float*>(address);
            break;
        case Reflection::PropertyType::String:
            *static_cast<std::string*>(out) = *static_cast<const std::string*>(address);
            break;
        default:
            break;
        }
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

        switch (property.type) {
        case Reflection::PropertyType::Bool:
            *static_cast<bool*>(address) = *static_cast<const bool*>(in);
            break;
        case Reflection::PropertyType::Int:
            *static_cast<int*>(address) = *static_cast<const int*>(in);
            break;
        case Reflection::PropertyType::Float:
            *static_cast<float*>(address) = *static_cast<const float*>(in);
            break;
        case Reflection::PropertyType::String:
            *static_cast<std::string*>(address) = *static_cast<const std::string*>(in);
            break;
        default:
            break;
        }
    }

    void ScriptComponent::ReleaseScriptObject()
    {
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
