#include "pch.h"
#include "UI/UIButtonComponent.h"

#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/GameObject.h"
#include "UI/RectTransformComponent.h"
#include "UI/UIImageComponent.h"
#include "Utility/FrameRate/Time.h"

REFLECT_REGISTER(CoreEngine::UIButtonComponent)
COMPONENT_REGISTER(CoreEngine::UIButtonComponent)

namespace CoreEngine
{
    void UIButtonComponent::Awake()
    {
        if (GameObject* const owner = GetOwner()) {
            owner->GetOrAddComponent<RectTransformComponent>();
        }
        ApplyVisual();
    }

    void UIButtonComponent::OnPropertyChanged(const Reflection::PropertyDescriptor& property)
    {
        (void)property;
        ApplyVisual();
    }

    bool UIButtonComponent::RequiresComponent(const IComponent& other) const
    {
        return dynamic_cast<const RectTransformComponent*>(&other) != nullptr;
    }

    bool UIButtonComponent::WasClicked() const
    {
        // 決まったのは前のフレームの後ろ半分。読む側はその次のフレームの前半に見る
        return clickedFrame_ != 0 && Time::FrameCount() == clickedFrame_ + 1;
    }

    void UIButtonComponent::SetInteractable(bool value)
    {
        if (interactable_ == value) {
            return;
        }
        interactable_ = value;
        if (!interactable_) {
            // 押せなくした瞬間は、乗っている・押している状態を持ち越さない
            hovered_ = false;
            pressed_ = false;
        }
        ApplyVisual();
    }

    RectTransformComponent* UIButtonComponent::GetRectTransform() const
    {
        return Sibling<RectTransformComponent>();
    }

    int UIButtonComponent::GetPointerSortOrder() const
    {
        const RectTransformComponent* const rect = GetRectTransform();
        return rect ? rect->GetSortOrder() : 0;
    }

    void UIButtonComponent::OnPointerEnter()
    {
        hovered_ = true;
        ApplyVisual();
    }

    void UIButtonComponent::OnPointerExit()
    {
        hovered_ = false;
        ApplyVisual();
    }

    void UIButtonComponent::OnPointerDown()
    {
        pressed_ = true;
        ApplyVisual();
    }

    void UIButtonComponent::OnPointerUp(bool onSelf)
    {
        pressed_ = false;
        if (onSelf && interactable_) {
            clickedFrame_ = Time::FrameCount();
        }
        ApplyVisual();
    }

    const Vector4& UIButtonComponent::CurrentColor() const
    {
        if (!interactable_) { return disabledColor_; }
        if (pressed_)       { return pressedColor_; }
        if (hovered_)       { return hoveredColor_; }
        return normalColor_;
    }

    void UIButtonComponent::ApplyVisual()
    {
        if (UIImageComponent* const image = Sibling<UIImageComponent>()) {
            image->SetColor(CurrentColor());
        }
    }
}
