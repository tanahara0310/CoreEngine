#include "pch.h"
#include "UI/UIToggleComponent.h"

#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "UI/RectTransformComponent.h"
#include "UI/UIImageComponent.h"
#include "Utility/FrameRate/Time.h"

REFLECT_REGISTER(CoreEngine::UIToggleComponent)
COMPONENT_REGISTER(CoreEngine::UIToggleComponent)

namespace CoreEngine
{
    void UIToggleComponent::Awake()
    {
        if (GameObject* const owner = GetOwner()) {
            owner->GetOrAddComponent<RectTransformComponent>();
        }
        ApplyVisual();
    }

    void UIToggleComponent::OnPropertyChanged(const Reflection::PropertyDescriptor& property)
    {
        (void)property;
        ApplyVisual();
    }

    bool UIToggleComponent::RequiresComponent(const IComponent& other) const
    {
        return dynamic_cast<const RectTransformComponent*>(&other) != nullptr;
    }

    bool UIToggleComponent::WasChanged() const
    {
        return changedFrame_ != 0 && Time::FrameCount() == changedFrame_ + 1;
    }

    void UIToggleComponent::SetIsOn(bool isOn)
    {
        if (isOn_ == isOn) {
            return;
        }
        isOn_ = isOn;
        changedFrame_ = Time::FrameCount();
        if (isOn_) {
            TurnOffGroupSiblings();
        }
        ApplyVisual();
    }

    void UIToggleComponent::SetInteractable(bool value)
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

    RectTransformComponent* UIToggleComponent::GetRectTransform() const
    {
        return Sibling<RectTransformComponent>();
    }

    int UIToggleComponent::GetPointerSortOrder() const
    {
        const RectTransformComponent* const rect = GetRectTransform();
        return rect ? rect->GetSortOrder() : 0;
    }

    void UIToggleComponent::OnPointerEnter()
    {
        hovered_ = true;
        ApplyVisual();
    }

    void UIToggleComponent::OnPointerExit()
    {
        hovered_ = false;
        ApplyVisual();
    }

    void UIToggleComponent::OnFocusEnter()
    {
        focused_ = true;
        ApplyVisual();
    }

    void UIToggleComponent::OnFocusExit()
    {
        focused_ = false;
        ApplyVisual();
    }

    void UIToggleComponent::OnPressBegin()
    {
        pressed_ = true;
        ApplyVisual();
    }

    void UIToggleComponent::OnPressEnd(bool onSelf)
    {
        pressed_ = false;
        if (onSelf && interactable_) {
            // グループに入っているものは、入りを切る側へは動かさない（全部切りにさせない）
            if (!isOn_ || group_.empty()) {
                SetIsOn(!isOn_);
            }
        }
        ApplyVisual();
    }

    const Vector4& UIToggleComponent::CurrentColor() const
    {
        if (!interactable_)       { return disabledColor_; }
        if (pressed_)             { return pressedColor_; }
        if (hovered_ || focused_) { return hoveredColor_; }
        return normalColor_;
    }

    void UIToggleComponent::ApplyVisual()
    {
        if (UIImageComponent* const image = Sibling<UIImageComponent>()) {
            image->SetColor(CurrentColor());
        }
        // 無効にしたコンポーネントは描かれないので、印の出し入れはこれで足りる
        if (UIImageComponent* const mark = checkmark_.Get()) {
            mark->SetEnabled(isOn_);
        }
    }

    void UIToggleComponent::TurnOffGroupSiblings()
    {
        if (group_.empty()) {
            return;
        }
        const GameObject* const owner = GetOwner();
        GameObjectManager* const manager = owner ? owner->GetObjectManager() : nullptr;
        if (!manager) {
            return;
        }
        manager->ForEachComponent<UIToggleComponent>([this](UIToggleComponent& other) {
            if (&other != this && other.group_ == group_) {
                other.SetIsOn(false);
            }
            });
    }
}
