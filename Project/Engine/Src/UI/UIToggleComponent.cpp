#include "pch.h"
#include "UI/UIToggleComponent.h"

#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "UI/UIImageComponent.h"
#include "Utility/FrameRate/Time.h"

REFLECT_REGISTER(CoreEngine::UIToggleComponent)
COMPONENT_REGISTER(CoreEngine::UIToggleComponent)

namespace CoreEngine
{
    UIToggleComponent::UIToggleComponent()
        : UISelectableComponent({ 1.0f, 1.0f, 1.0f, 1.0f }, { 0.85f, 0.9f, 1.0f, 1.0f },
                                { 0.7f, 0.78f, 0.95f, 1.0f }, { 0.6f, 0.6f, 0.6f, 0.5f })
    {
    }

    bool UIToggleComponent::WasChanged() const
    {
        return IsFrameAfter(changedFrame_);
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

    void UIToggleComponent::OnClick()
    {
        // グループに入っているものは、入りを切る側へは動かさない（全部切りにさせない）
        if (!isOn_ || group_.empty()) {
            SetIsOn(!isOn_);
        }
    }

    void UIToggleComponent::ApplyVisual()
    {
        UISelectableComponent::ApplyVisual();
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
