#include "pch.h"
#include "UI/UIMaskComponent.h"

#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/GameObject.h"
#include "UI/RectTransformComponent.h"
#include "Utility/Logger/Logger.h"

#include <string>

REFLECT_REGISTER(CoreEngine::UIMaskComponent)
COMPONENT_REGISTER(CoreEngine::UIMaskComponent)

namespace CoreEngine
{
    bool UIMaskComponent::RequiresComponent(const IComponent& other) const
    {
        return IsAnyOf<RectTransformComponent>(other);
    }

    void UIMaskComponent::WarnRotatedOnce() const
    {
        if (rotatedWarned_) {
            return;
        }
        rotatedWarned_ = true;
        const GameObject* const owner = GetOwner();
        Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Graphics,
            "UIMask: \"{}\" は回転しているので、回転を見ずに囲む四角で切ります",
            owner ? owner->GetName() : std::string("?"));
    }

    void UIMaskComponent::Awake()
    {
        if (GameObject* const owner = GetOwner()) {
            owner->GetOrAddComponent<RectTransformComponent>();
        }
    }
}
