#include "pch.h"
#include "UI/UIDropdownItemComponent.h"

#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/GameObject.h"
#include "UI/RectTransformComponent.h"
#include "UI/UIDropdownComponent.h"

REFLECT_DEFINE_BEGIN(CoreEngine::UIDropdownItemComponent, "UI ドロップダウンの行")
    REFLECT_OBJECT_REF(checkmark_, "選んでいる行に出す印")
    REFLECT_PROPERTY(normalColor_, "通常の色",
        p.type = ::CoreEngine::Reflection::PropertyType::Color)
    REFLECT_PROPERTY(hoveredColor_, "乗せたときの色",
        p.type = ::CoreEngine::Reflection::PropertyType::Color,
        p.tooltip = "キー・パッドで選んでいるときもこの色になる")
    REFLECT_PROPERTY(pressedColor_, "押したときの色",
        p.type = ::CoreEngine::Reflection::PropertyType::Color)
    REFLECT_PROPERTY(disabledColor_, "押せないときの色",
        p.type = ::CoreEngine::Reflection::PropertyType::Color)
REFLECT_DEFINE_END()
REFLECT_REGISTER(CoreEngine::UIDropdownItemComponent)
COMPONENT_REGISTER(CoreEngine::UIDropdownItemComponent)

namespace CoreEngine
{
    UIDropdownItemComponent::UIDropdownItemComponent()
        : UISelectableComponent({ 1.0f, 1.0f, 1.0f, 1.0f }, { 0.85f, 0.90f, 1.0f, 1.0f },
                                { 0.65f, 0.72f, 0.85f, 1.0f }, { 0.45f, 0.45f, 0.45f, 0.6f })
    {
    }

    void UIDropdownItemComponent::Bind(UIDropdownComponent* dropdown, int index)
    {
        dropdown_.Set(dropdown);
        index_ = index;
    }

    UIDropdownComponent* UIDropdownItemComponent::GetDropdown() const
    {
        return dropdown_.Get();
    }

    void UIDropdownItemComponent::SetCheckmark(RectTransformComponent* checkmark)
    {
        checkmark_.Set(checkmark);
    }

    void UIDropdownItemComponent::SetSelected(bool selected)
    {
        selected_ = selected;
        ApplyVisual();
    }

    void UIDropdownItemComponent::OnClick()
    {
        if (UIDropdownComponent* const dropdown = dropdown_.Get()) {
            dropdown->ChooseFromList(index_);
        }
    }

    void UIDropdownItemComponent::ApplyVisual()
    {
        UISelectableComponent::ApplyVisual();
        // ひな形の印は編集で見えるように残す
        if (index_ < 0) {
            return;
        }
        if (RectTransformComponent* const mark = checkmark_.Get()) {
            if (GameObject* const owner = mark->GetOwner()) {
                owner->SetActive(selected_);
            }
        }
    }
}
