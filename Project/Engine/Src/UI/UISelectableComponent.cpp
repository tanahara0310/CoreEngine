#include "pch.h"
#include "UI/UISelectableComponent.h"

#include "GameObject/GameObject.h"
#include "UI/RectTransformComponent.h"
#include "UI/UIImageComponent.h"
#include "Utility/FrameRate/Time.h"

namespace CoreEngine
{
    UISelectableComponent::UISelectableComponent(const Vector4& normalColor, const Vector4& hoveredColor,
                                                 const Vector4& pressedColor, const Vector4& disabledColor)
        : normalColor_(normalColor)
        , hoveredColor_(hoveredColor)
        , pressedColor_(pressedColor)
        , disabledColor_(disabledColor)
    {
    }

    void UISelectableComponent::Awake()
    {
        if (GameObject* const owner = GetOwner()) {
            owner->GetOrAddComponent<RectTransformComponent>();
        }
        ApplyVisual();
    }

    void UISelectableComponent::OnPropertyChanged(const Reflection::PropertyDescriptor& property)
    {
        (void)property;
        ApplyVisual();
    }

    bool UISelectableComponent::RequiresComponent(const IComponent& other) const
    {
        return IsAnyOf<RectTransformComponent>(other);
    }

    void UISelectableComponent::SetInteractable(bool value)
    {
        if (interactable_ == value) {
            return;
        }
        interactable_ = value;
        if (!interactable_) {
            // 押せなくした瞬間は、乗っている・選んでいる・押している状態を持ち越さない
            hovered_ = false;
            focused_ = false;
            pressed_ = false;
        }
        ApplyVisual();
    }

    RectTransformComponent* UISelectableComponent::GetRectTransform() const
    {
        return Sibling<RectTransformComponent>();
    }

    int UISelectableComponent::GetPointerSortOrder() const
    {
        const RectTransformComponent* const rect = GetRectTransform();
        return rect ? rect->GetSortOrder() : 0;
    }

    void UISelectableComponent::OnPointerEnter()
    {
        hovered_ = true;
        ApplyVisual();
    }

    void UISelectableComponent::OnPointerExit()
    {
        hovered_ = false;
        ApplyVisual();
    }

    void UISelectableComponent::OnFocusEnter()
    {
        focused_ = true;
        ApplyVisual();
    }

    void UISelectableComponent::OnFocusExit()
    {
        focused_ = false;
        ApplyVisual();
    }

    void UISelectableComponent::OnPressBegin()
    {
        pressed_ = true;
        ApplyVisual();
    }

    void UISelectableComponent::OnPressEnd(bool onSelf)
    {
        pressed_ = false;
        if (onSelf && interactable_) {
            OnClick();
        }
        ApplyVisual();
    }

    void UISelectableComponent::ApplyVisual()
    {
        if (UIImageComponent* const image = Sibling<UIImageComponent>()) {
            image->SetColor(CurrentColor());
        }
    }

    const Vector4& UISelectableComponent::CurrentColor() const
    {
        if (!interactable_)       { return disabledColor_; }
        if (pressed_)             { return pressedColor_; }
        // 選んでいるときは乗せたときと同じ色にする
        if (hovered_ || focused_) { return hoveredColor_; }
        return normalColor_;
    }

    bool UISelectableComponent::IsFrameAfter(std::uint64_t recordedFrame)
    {
        // 決まったのは前のフレームの後ろ半分。読む側はその次のフレームの前半に見る
        return recordedFrame != 0 && Time::FrameCount() == recordedFrame + 1;
    }
}
