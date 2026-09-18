#include "pch.h"
#include "LightingFeature.h"
#include "EngineSystem/EngineSystem.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/Render/RenderManager.h"

namespace CoreEngine
{
    void LightingFeature::Initialize(SceneContext& ctx)
    {
        // デフォルトのディレクショナルライトを設定
        lightManager_ = ctx.engine->GetService<LightManager>();
        if (lightManager_) {
            directionalLightHandle_ = lightManager_->CreateLight(LightType::Directional, "Sun");
            if (Light* light = lightManager_->GetLight(directionalLightHandle_)) {
                light->color = { 1.0f, 1.0f, 1.0f };
                // 高度約 40 度・南西向き
                light->direction = CoreEngine::Normalize(Vector3{ -0.45073172f, -0.65011942f, 0.61170721f });
                // 快晴の太陽直下照度。既定背景（大気散乱）の空の輝度スケールと合う値
                light->intensity = LightUnits::kSunIlluminanceLux;
                light->enabled = true;
                // 既定背景（大気散乱）の太陽として扱う。
                // キューブマップモードのシーンでは大気が非アクティブのため影響はない。
                light->isAtmosphereSun = true;
                // 空の輝度スケール（0 にすると照度からの自動換算）。
                // 空を明るくしたいシーンは atmosphereIntensity のみを上げること。
                // intensity（サーフェス直接光）を上げるとアルベドの明るい面が ACES の飽和域へ入る。
                light->atmosphereIntensity = kDefaultSunAtmosphereIntensity;
            }
        }
    }

    Light* LightingFeature::GetDirectionalLight() const
    {
        return lightManager_ ? lightManager_->GetLight(directionalLightHandle_) : nullptr;
    }

    void LightingFeature::Update(SceneContext& ctx, SceneUpdatePhase phase)
    {
        if (phase != SceneUpdatePhase::FrameStart) {
            return;
        }

        auto lightManager = ctx.engine->GetService<LightManager>();
        if (lightManager) {
            lightManager->UpdateAll();
        }
    }
}
