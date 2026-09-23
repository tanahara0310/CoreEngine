#include "pch.h"
#include "GameObject/Component/Core/ObjectRef.h"

#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    IComponent* FindReferencedComponent(const GameObject& object,
        Reflection::PropertyDescriptor::ComponentFilter accepts, std::string_view componentType)
    {
        IComponent* firstAccepted = nullptr;
        for (const auto& slot : object.GetAllComponents()) {
            IComponent* component = slot.get();
            if (!component || (accepts && !accepts(component))) {
                continue;
            }
            if (!componentType.empty() && componentType == component->GetTypeName()) {
                return component;
            }
            if (!firstAccepted) {
                firstAccepted = component;
            }
        }
        return firstAccepted;
    }

    IComponent* FindReferencedComponent(const GameObject& object,
        const Reflection::PropertyDescriptor& property, std::string_view componentType)
    {
        IComponent* firstAccepted = nullptr;
        for (const auto& slot : object.GetAllComponents()) {
            IComponent* component = slot.get();
            if (!component || (property.acceptsComponent && !property.acceptsComponent(component))) {
                continue;
            }
            if (property.acceptsComponentType && std::string_view(property.acceptsComponentType) != component->GetTypeName()) {
                continue;
            }
            if (!componentType.empty() && componentType == component->GetTypeName()) {
                return component;
            }
            if (!firstAccepted) {
                firstAccepted = component;
            }
        }
        return firstAccepted;
    }

    Reflection::ObjectRefValue ObjectRefBase::GetValue() const
    {
        return Reflection::ObjectRefValue{ objectId_, componentType_ };
    }

    void ObjectRefBase::SetValue(const Reflection::ObjectRefValue& value, const IComponent* holder)
    {
        objectId_ = value.objectId;
        componentType_ = value.componentType;
        cached_ = nullptr;

        const GameObject* owner = holder ? holder->GetOwner() : nullptr;
        if (owner && owner->GetObjectManager()) {
            BindScene(owner->GetObjectManager());
        }
    }

    void ObjectRefBase::Reset()
    {
        objectId_ = ObjectId{};
        componentType_.clear();
        cached_ = nullptr;
    }

    void ObjectRefBase::Assign(IComponent* target)
    {
        if (!target) {
            Reset();
            return;
        }

        const GameObject* owner = target->GetOwner();
        if (!owner || !owner->GetObjectId().IsValid() || !owner->GetObjectManager()) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                "ObjectRef: シーンに登録されていないオブジェクトのコンポーネント \"{}\" は指せません",
                target->GetTypeName());
            Reset();
            return;
        }

        objectId_ = owner->GetObjectId();
        componentType_ = target->GetTypeName();
        BindScene(owner->GetObjectManager());
        cached_ = target;
        cachedEpoch_ = *epoch_;
    }

    IComponent* ObjectRefBase::Resolve(Reflection::PropertyDescriptor::ComponentFilter accepts) const
    {
        cached_ = nullptr;
        if (!objectId_.IsValid() || !manager_) {
            return nullptr;
        }

        if (const GameObject* object = manager_->FindObject(objectId_)) {
            cached_ = FindReferencedComponent(*object, accepts, componentType_);
        }
        cachedEpoch_ = *epoch_;
        return cached_;
    }

    void ObjectRefBase::BindScene(const GameObjectManager* manager)
    {
        manager_ = manager;
        epoch_ = manager ? &manager->GetReferenceEpoch() : nullptr;
    }
}
