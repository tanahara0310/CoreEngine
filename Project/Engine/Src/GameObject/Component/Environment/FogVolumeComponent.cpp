#include "pch.h"
#include "GameObject/Component/Environment/FogVolumeComponent.h"

#include "GameObject/GameObject.h"
#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "Graphics/Light/Light.h"

#include <algorithm>
#include <cmath>

REFLECT_REGISTER(CoreEngine::FogVolumeComponent)
COMPONENT_REGISTER(CoreEngine::FogVolumeComponent)

namespace CoreEngine
{
    void FogVolumeComponent::Awake()
    {
        // 位置・向き・スケールはオブジェクトが持つ（ギズモで動かせて、保存データにも残る）
        if (GameObject* const owner = GetOwner()) {
            owner->GetOrAddComponent<TransformComponent>();
        }
    }

    VolumetricFogVolumeDesc FogVolumeComponent::BuildDesc() const
    {
        VolumetricFogVolumeDesc desc{};
        desc.shape = (shape_ == Shape::Sphere)
            ? VolumetricFog::kVolumeShapeSphere : VolumetricFog::kVolumeShapeBox;

        Vector3 scale{ 1.0f, 1.0f, 1.0f };
        if (const GameObject* const owner = GetOwner()) {
            desc.center = owner->GetWorldPosition();
            owner->GetWorldAxes(desc.axisX, desc.axisY, desc.axisZ);
            scale = owner->GetWorldScale();
        }
        desc.size = Vector3{
            std::max(size_.x * std::abs(scale.x), 0.0f),
            std::max(size_.y * std::abs(scale.y), 0.0f),
            std::max(size_.z * std::abs(scale.z), 0.0f) };

        desc.edgeFade = std::clamp(edgeFade_, 0.0f, 1.0f);
        desc.extinction = std::max(density_, 0.0f);
        desc.albedo = Vector3{
            std::max(albedo_.x, 0.0f), std::max(albedo_.y, 0.0f), std::max(albedo_.z, 0.0f) };
        // 輝度 [nt] とシェーダー単位の輝度は照度と同じ係数で写す
        const float emissive = std::max(emissiveIntensity_, 0.0f) * LightUnits::kShaderUnitsPerLux;
        desc.emissive = Vector3{
            std::max(emissiveColor_.x, 0.0f) * emissive,
            std::max(emissiveColor_.y, 0.0f) * emissive,
            std::max(emissiveColor_.z, 0.0f) * emissive };
        desc.noiseStrength = std::clamp(noiseStrength_, 0.0f, 1.0f);
        return desc;
    }
}
