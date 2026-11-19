#include "pch.h"
#include "UI/UILayoutGroupComponent.h"

#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/GameObject.h"
#include "UI/RectTransformComponent.h"
#include "UI/UILayoutPass.h"

REFLECT_REGISTER(CoreEngine::UILayoutGroupComponent)
COMPONENT_REGISTER(CoreEngine::UILayoutGroupComponent)

namespace CoreEngine
{
    UILayoutGroupComponent::UILayoutGroupComponent()
    {
        UILayoutPass::NotifyCreated();
    }

    UILayoutGroupComponent::~UILayoutGroupComponent()
    {
        UILayoutPass::NotifyDestroyed();
    }

    bool UILayoutGroupComponent::RequiresComponent(const IComponent& other) const
    {
        return IsAnyOf<RectTransformComponent>(other);
    }

    void UILayoutGroupComponent::Awake()
    {
        if (GameObject* const owner = GetOwner()) {
            owner->GetOrAddComponent<RectTransformComponent>();
        }
    }

    bool UILayoutGroupComponent::ControlsChildSize(int axis) const
    {
        if (layoutType_ == UILayoutType::Grid) {
            return true;
        }
        return axis == 0 ? controlChildWidth_ : controlChildHeight_;
    }

    bool UILayoutGroupComponent::ForcesExpand(int axis) const
    {
        if (layoutType_ == UILayoutType::Grid) {
            return false;
        }
        return axis == 0 ? childForceExpandWidth_ : childForceExpandHeight_;
    }

    float UILayoutGroupComponent::GetAlignmentRatio(int axis) const
    {
        const Vector2 ratio = GetAnchorNormalized(childAlignment_);
        return axis == 0 ? ratio.x : ratio.y;
    }
}
