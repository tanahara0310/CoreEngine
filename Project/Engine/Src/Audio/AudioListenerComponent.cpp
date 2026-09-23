#include "pch.h"
#include "Audio/AudioListenerComponent.h"

#include "GameObject/GameObject.h"
#include "GameObject/Component/Core/ComponentFactory.h"
#include "Utility/Logger/Logger.h"

REFLECT_REGISTER(CoreEngine::AudioListenerComponent)
COMPONENT_REGISTER(CoreEngine::AudioListenerComponent)

namespace CoreEngine
{
    AudioListenerComponent* AudioListenerComponent::active_ = nullptr;

    void AudioListenerComponent::Awake()
    {
        if (active_ && active_ != this) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Audio,
                "音の聞き手が 2 つ以上あります。先に置いた方を使います");
            return;
        }
        active_ = this;
    }

    void AudioListenerComponent::OnDestroy()
    {
        if (active_ == this) {
            active_ = nullptr;
        }
    }

    Vector3 AudioListenerComponent::GetWorldPosition() const
    {
        const GameObject* const owner = GetOwner();
        return owner ? owner->GetWorldPosition() : Vector3{};
    }

    Vector3 AudioListenerComponent::GetRightAxis() const
    {
        const GameObject* const owner = GetOwner();
        if (!owner) {
            return Vector3{ 1.0f, 0.0f, 0.0f };
        }
        Vector3 axisX{};
        Vector3 axisY{};
        Vector3 axisZ{};
        owner->GetWorldAxes(axisX, axisY, axisZ);
        return axisX;
    }
}
