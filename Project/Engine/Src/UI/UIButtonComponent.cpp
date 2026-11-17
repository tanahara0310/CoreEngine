#include "pch.h"
#include "UI/UIButtonComponent.h"

#include "GameObject/Component/Core/ComponentFactory.h"
#include "Utility/FrameRate/Time.h"

REFLECT_REGISTER(CoreEngine::UIButtonComponent)
COMPONENT_REGISTER(CoreEngine::UIButtonComponent)

namespace CoreEngine
{
    UIButtonComponent::UIButtonComponent()
        : UISelectableComponent({ 1.0f, 1.0f, 1.0f, 1.0f }, { 0.85f, 0.90f, 1.0f, 1.0f },
                                { 0.65f, 0.72f, 0.85f, 1.0f }, { 0.45f, 0.45f, 0.45f, 0.6f })
    {
    }

    bool UIButtonComponent::WasClicked() const
    {
        return IsFrameAfter(clickedFrame_);
    }

    void UIButtonComponent::OnClick()
    {
        clickedFrame_ = Time::FrameCount();
    }
}
