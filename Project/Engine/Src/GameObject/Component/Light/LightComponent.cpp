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
    std::vector<LightComponent*> LightComponent::instances_;

    LightComponent::LightComponent()
        : LightComponent(LightType::Point)
    {
    }

    LightComponent::LightComponent(LightType type)
    {
        // 足した直後から見える値にする（種類ごとの既定は LightManager が単一情報源）
        LightManager::SetupDefaults(light_, type);
    }

    LightComponent::~LightComponent()
    {
        std::erase(instances_, this);
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
        if (manager->GetLight(handle_)) {
            instances_.push_back(this);
            SyncWithManager();
        }
    }

    void LightComponent::OnEnable()
    {
        SyncWithManager();
    }

    void LightComponent::OnDisable()
    {
        SyncWithManager();
    }

    void LightComponent::SyncWithManager()
    {
        LightManager* const manager = ResolveManager();
        const Light* const live = manager ? manager->GetLight(handle_) : nullptr;
        if (!live) {
            return;
        }

        // 種類は LightManager を通して変える。断られたら実体の種類へ戻す
        if (light_.type != live->type && !manager->ChangeType(handle_, light_.type)) {
            light_.type = live->type;
        }

        // 位置・有効・名前はオブジェクト側が持つ
        Light values = light_;
        values.enabled = IsActiveAndEnabled();
        if (GameObject* const owner = GetOwner()) {
            values.position = owner->GetWorldPosition();
            if (owner->GetName() != ownerName_) {
                ownerName_ = owner->GetName();
                manager->SetLightName(handle_, ownerName_);
            }
        }
        manager->UpdateLight(handle_, std::move(values));
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

    const Light* LightComponent::GetLight() const
    {
        const LightManager* const manager = ResolveManager();
        return manager ? manager->GetLight(handle_) : nullptr;
    }

    LightComponent* LightComponent::Find(LightHandle handle)
    {
        if (!handle.IsValid()) {
            return nullptr;
        }
        for (LightComponent* const component : instances_) {
            if (component->handle_ == handle) {
                return component;
            }
        }
        return nullptr;
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
}
