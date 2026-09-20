#include "pch.h"
#include "GameObject/Component/Camera/CameraComponent.h"

#include "Camera/Camera.h"
#include "GameObject/GameObject.h"
#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/Component/Transform/TransformComponent.h"

REFLECT_REGISTER(CoreEngine::CameraComponent)
COMPONENT_REGISTER(CoreEngine::CameraComponent)

namespace CoreEngine
{
    void CameraComponent::Awake()
    {
        // 構図はオブジェクトが持つ（ギズモで動かせて、保存データにも残る）
        if (GameObject* const owner = GetOwner()) {
            owner->GetOrAddComponent<TransformComponent>();
        }
    }

    void CameraComponent::ApplyTo(Camera& camera) const
    {
        GameObject* const owner = GetOwner();
        if (const TransformComponent* const transform =
                owner ? owner->GetComponent<TransformComponent>() : nullptr) {
            camera.SetTranslate(transform->GetWorldPosition());
            camera.SetRotate(transform->Get().rotate);
        }
        camera.SetParameters(parameters_);
    }
}
