#include "pch.h"
#include "UI/UILayoutElementComponent.h"

#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/GameObject.h"
#include "UI/RectTransformComponent.h"

REFLECT_REGISTER(CoreEngine::UILayoutElementComponent)
COMPONENT_REGISTER(CoreEngine::UILayoutElementComponent)

namespace CoreEngine
{
    bool UILayoutElementComponent::RequiresComponent(const IComponent& other) const
    {
        return IsAnyOf<RectTransformComponent>(other);
    }

    void UILayoutElementComponent::Awake()
    {
        if (GameObject* const owner = GetOwner()) {
            owner->GetOrAddComponent<RectTransformComponent>();
        }
    }
}
