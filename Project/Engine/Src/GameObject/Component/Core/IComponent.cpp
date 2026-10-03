#include "pch.h"
#include "IComponent.h"

#include "GameObject/GameObject.h"

namespace CoreEngine
{
    void IComponent::SetEnabled(bool enabled)
    {
        if (isEnabled_ == enabled) {
            return;
        }
        isEnabled_ = enabled;
        RefreshEnabledNotification();
    }

    void IComponent::RefreshEnabledNotification()
    {
        const bool enabled = lifecycle_ == Lifecycle::Awake && isEnabled_ && (!owner_ || owner_->IsActive());
        if (enabled == enableNotified_) {
            return;
        }
        enableNotified_ = enabled;
        if (enabled) {
            OnEnable();
        } else {
            OnDisable();
        }
    }

    void IComponent::NotifyDisabled()
    {
        if (!enableNotified_) {
            return;
        }
        enableNotified_ = false;
        OnDisable();
    }
}
