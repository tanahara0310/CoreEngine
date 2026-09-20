#include "pch.h"
#include "LightingFeature.h"
#include "EngineSystem/EngineSystem.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "GameObject/Component/Light/LightComponent.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/Render/RenderManager.h"

#include <memory>

namespace CoreEngine
{
    void LightingFeature::Initialize(SceneContext& ctx)
    {
        lightManager_ = ctx.engine ? ctx.engine->GetService<LightManager>() : nullptr;
        CreateDefaultSun(ctx);
    }

    void LightingFeature::CreateDefaultSun(SceneContext& ctx)
    {
        if (!ctx.gameObjectManager) {
            return;
        }

        auto owned = std::make_unique<GameObject>();
        owned->SetName(kDefaultSunObjectName);
        GameObject* const object = ctx.gameObjectManager->AddObject(std::move(owned));
        LightComponent* const component = object ? object->AddComponent<LightComponent>() : nullptr;
        if (!component) {
            return;
        }

        Light& light = component->Get();
        light.type = LightType::Directional;
        light.color = { 1.0f, 1.0f, 1.0f };
        // 高度約 40 度・南西向き
        light.direction = CoreEngine::Normalize(Vector3{ -0.45073172f, -0.65011942f, 0.61170721f });
        // 快晴の太陽直下照度。既定背景（大気散乱）の空の輝度スケールと合う値
        light.intensity = LightUnits::kSunIlluminanceLux;
        // 既定背景（大気散乱）の太陽として扱う。
        // キューブマップモードのシーンでは大気が非アクティブのため影響はない。
        light.isAtmosphereSun = true;
        // 空の輝度スケール（0 にすると照度からの自動換算）。
        // 空を明るくしたいシーンは atmosphereIntensity のみを上げること。
        // intensity（サーフェス直接光）を上げるとアルベドの明るい面が ACES の飽和域へ入る。
        light.atmosphereIntensity = kDefaultSunAtmosphereIntensity;

        // シーンのコードが OnInitialize から GetDirectionalLight() で触れるよう、実体へ先に写す
        component->SyncWithManager();
        defaultSun_ = object;
    }

    void LightingFeature::PostSceneInitialize(SceneContext& ctx)
    {
        if (!defaultSun_ || !ctx.gameObjectManager) {
            return;
        }

        // シーンが自前の平行光源を置いていたら、既定の太陽は引っ込む
        // （保存データが既定の太陽と同じ名前で復元された場合は同じオブジェクトなので残る）
        const LightComponent* const own = defaultSun_->GetComponent<LightComponent>();
        bool sceneHasDirectional = false;
        ctx.gameObjectManager->ForEachComponent<LightComponent>(
            [&](LightComponent& component) {
                if (&component != own && component.GetLightType() == LightType::Directional) {
                    sceneHasDirectional = true;
                }
            });

        if (sceneHasDirectional) {
            defaultSun_->Destroy();
            defaultSun_ = nullptr;
        }
    }

    Light* LightingFeature::GetDirectionalLight() const
    {
        // 大気の太陽が無ければ最初の平行光源へフォールバックする（LightManager 側の規則）
        return lightManager_ ? lightManager_->GetAtmosphereSunLight() : nullptr;
    }

    void LightingFeature::Update(SceneContext& ctx, SceneUpdatePhase phase)
    {
        if (phase != SceneUpdatePhase::FrameStart) {
            return;
        }

        // 無効なコンポーネント・非アクティブなオブジェクトのライトも消灯させる必要があるので、
        // ForEachComponent（有効なものだけを回す）ではなく自分で走査する
        if (ctx.gameObjectManager) {
            for (const auto& object : ctx.gameObjectManager->GetAllObjects()) {
                if (!object || object->IsMarkedForDestroy()) {
                    continue;
                }
                for (const auto& slot : object->GetAllComponents()) {
                    if (auto* const light = dynamic_cast<LightComponent*>(slot.get())) {
                        light->SyncWithManager();
                    }
                }
            }
        }

        if (lightManager_) {
            lightManager_->UpdateAll();
        }
    }
}
