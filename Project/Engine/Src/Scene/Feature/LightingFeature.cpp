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
                light->direction = CoreEngine::Normalize(Vector3{ -0.45073172f, -0.65011942f, 0.61170721f });
                // 高度約 40 度・南西向き。以前は真下(0,-1,0)を既定にし、実効値は
                // r.AtmosphereLights.SunDirection の保存値から復元していたが、
                // ミラー値の保存をやめた（NoSave 化）ため既定側へ移した
                // 旧既定（シェーダー単位 intensity=1.0）と同輝度の照度（≈ 57,143 lx）。
                // 大気シーンは LightUnits::kSunIlluminanceLux（100,000 lx）へ上書きする。
                light->intensity = 1.0f / LightUnits::kShaderUnitsPerLux;
                light->enabled = true;
                // 既定背景（大気散乱）の太陽として扱う。
                // キューブマップモードのシーンでは大気が非アクティブのため影響はない。
                light->isAtmosphereSun = true;
                // 0 = 空の輝度スケールを照度からの自動換算にフォールバックさせる（従来動作）。
                // 空を明るくしたいシーンは atmosphereIntensity のみを上げること。
                // intensity（サーフェス直接光）を上げるとアルベドの明るい面が ACES の飽和域へ入る。
                light->atmosphereIntensity = 0.0f;
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
