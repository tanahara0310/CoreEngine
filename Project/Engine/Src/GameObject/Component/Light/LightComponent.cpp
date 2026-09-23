#include "pch.h"
#include "GameObject/Component/Light/LightComponent.h"

#include "EngineSystem/EngineSystem.h"
#include "GameObject/GameObject.h"
#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "Graphics/Light/LightManager.h"

REFLECT_REGISTER(CoreEngine::LightComponent)
COMPONENT_REGISTER(CoreEngine::LightComponent)

namespace CoreEngine
{
    LightComponent::LightComponent()
    {
        // 足した直後から見える値にする（種類ごとの既定は LightManager が単一情報源）
        LightManager::SetupDefaults(light_, LightType::Point);
    }

    LightComponent::~LightComponent()
    {
        if (LightManager* const manager = ResolveManager()) {
            manager->DestroyLight(handle_);
        }
    }

    void LightComponent::Awake()
    {
        LightManager* const manager = ResolveManager();
        if (!manager) {
            return;
        }

        GameObject* const owner = GetOwner();
        ownerName_ = owner ? owner->GetName() : std::string{};

        // 位置はオブジェクトが持つ（ギズモで動かせて、保存データにも残る）
        if (owner) {
            owner->GetOrAddComponent<TransformComponent>();
        }

        // 生成時に種類ごとの既定が入るが、直後の同期でこちらの値へ上書きする
        handle_ = manager->CreateLight(light_.type, ownerName_);
        if (Light* const live = manager->GetLight(handle_)) {
            lastWritten_ = *live;
            SyncWithManager();
        }
    }

    void LightComponent::SyncWithManager()
    {
        LightManager* const manager = ResolveManager();
        Light* const live = manager ? manager->GetLight(handle_) : nullptr;
        if (!live) {
            return;
        }

        AdoptExternalEdits(*live);
        ApplyToLight(*live);
        lastWritten_ = *live;
    }

    Vector4 LightComponent::GetColor() const
    {
        return { light_.color.x, light_.color.y, light_.color.z, 1.0f };
    }

    void LightComponent::SetColor(const Vector4& color)
    {
        light_.color = { color.x, color.y, color.z };
    }

    void LightComponent::FocusGizmo() const
    {
        if (LightManager* const manager = ResolveManager()) {
            manager->SetGizmoFocusLight(handle_);
        }
    }

    Light* LightComponent::GetLight() const
    {
        LightManager* const manager = ResolveManager();
        return manager ? manager->GetLight(handle_) : nullptr;
    }

    LightManager* LightComponent::ResolveManager() const
    {
        if (lightManager_) {
            return lightManager_;
        }
        GameObject* const owner = GetOwner();
        EngineSystem* const engine = owner ? owner->GetEngineSystem() : nullptr;
        lightManager_ = engine ? engine->GetService<LightManager>() : nullptr;
        return lightManager_;
    }

    void LightComponent::AdoptExternalEdits(const Light& live)
    {
        // 前回書いた内容と違う項目だけを取り込む。こちらの値で毎フレーム塗り潰すと、
        // 実体を直に書き換えるエディタ（Lighting パネル・Sky Atmosphere）の編集が消える
        if (live.type != lastWritten_.type) {
            light_.type = live.type;
        }
        if (live.color != lastWritten_.color) {
            light_.color = live.color;
        }
        if (live.intensity != lastWritten_.intensity) {
            light_.intensity = live.intensity;
        }
        if (live.direction != lastWritten_.direction) {
            light_.direction = live.direction;
        }
        if (live.range != lastWritten_.range) {
            light_.range = live.range;
        }
        if (live.innerConeAngleDeg != lastWritten_.innerConeAngleDeg) {
            light_.innerConeAngleDeg = live.innerConeAngleDeg;
        }
        if (live.outerConeAngleDeg != lastWritten_.outerConeAngleDeg) {
            light_.outerConeAngleDeg = live.outerConeAngleDeg;
        }
        if (live.areaWidth != lastWritten_.areaWidth) {
            light_.areaWidth = live.areaWidth;
        }
        if (live.areaHeight != lastWritten_.areaHeight) {
            light_.areaHeight = live.areaHeight;
        }
        if (live.isAtmosphereSun != lastWritten_.isAtmosphereSun) {
            light_.isAtmosphereSun = live.isAtmosphereSun;
        }
        if (live.isAtmosphereMoon != lastWritten_.isAtmosphereMoon) {
            light_.isAtmosphereMoon = live.isAtmosphereMoon;
        }
        if (live.atmosphereIntensity != lastWritten_.atmosphereIntensity) {
            light_.atmosphereIntensity = live.atmosphereIntensity;
        }
        if (live.enabled != lastWritten_.enabled) {
            SetEnabled(live.enabled);
        }
        if (live.position != lastWritten_.position) {
            MoveOwnerTo(live.position);
        }
    }

    void LightComponent::ApplyToLight(Light& live)
    {
        GameObject* const owner = GetOwner();

        std::string name = std::move(live.name);
        live = light_;
        live.name = std::move(name);

        // 名前・位置・有効はオブジェクト側が持つ
        if (owner) {
            if (owner->GetName() != ownerName_) {
                ownerName_ = owner->GetName();
                live.name = ownerName_;
            }
            live.position = owner->GetWorldPosition();
            live.enabled = IsEnabled() && owner->IsActive();
        } else {
            live.enabled = IsEnabled();
        }
    }

    void LightComponent::MoveOwnerTo(const Vector3& position)
    {
        GameObject* const owner = GetOwner();
        auto* const transform = owner ? owner->GetOrAddComponent<TransformComponent>() : nullptr;
        if (transform) {
            transform->Get().translate = position;
        }
    }
}
