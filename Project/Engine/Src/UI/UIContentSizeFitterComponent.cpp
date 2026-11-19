#include "pch.h"
#include "UI/UIContentSizeFitterComponent.h"

#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/GameObject.h"
#include "UI/RectTransformComponent.h"
#include "UI/UILayoutPass.h"

REFLECT_REGISTER(CoreEngine::UIContentSizeFitterComponent)
COMPONENT_REGISTER(CoreEngine::UIContentSizeFitterComponent)

namespace CoreEngine
{
    UIContentSizeFitterComponent::UIContentSizeFitterComponent()
    {
        UILayoutPass::NotifyCreated();
    }

    UIContentSizeFitterComponent::~UIContentSizeFitterComponent()
    {
        UILayoutPass::NotifyDestroyed();
    }

    bool UIContentSizeFitterComponent::RequiresComponent(const IComponent& other) const
    {
        return IsAnyOf<RectTransformComponent>(other);
    }

    void UIContentSizeFitterComponent::Awake()
    {
        if (GameObject* const owner = GetOwner()) {
            owner->GetOrAddComponent<RectTransformComponent>();
        }
    }
}
