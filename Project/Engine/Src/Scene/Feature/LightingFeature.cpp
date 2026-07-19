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
        lightManager_ = ctx.engine->GetComponent<LightManager>();
        if (lightManager_) {
            directionalLightHandle_ = lightManager_->CreateLight(LightType::Directional, "Sun");
            if (Light* light = lightManager_->GetLight(directionalLightHandle_)) {
                light->color = { 1.0f, 1.0f, 1.0f };
                light->direction = MathCore::Vector::Normalize({ 0.0f, -1.0f, 0.0f });
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

        auto lightManager = ctx.engine->GetComponent<LightManager>();
        if (lightManager) {
            lightManager->UpdateAll();

            // シャドウマップ用のライトView-Projection行列を更新
            UpdateLightViewProjection(ctx);
        }
    }

    void LightingFeature::UpdateLightViewProjection(SceneContext& ctx)
    {
        // ディレクショナルライトが有効な場合のみ、シャドウマップ用の行列を計算
        Light* directionalLight = GetDirectionalLight();
        if (!directionalLight || !directionalLight->enabled) {
            return;
        }

        // ライト位置を計算（ライト方向の逆方向、シーン中心から一定距離）
        Vector3 lightDir = MathCore::Vector::Normalize(directionalLight->direction);
        Vector3 lightPos = MathCore::Vector::Multiply(-config_.shadowLightDistance, lightDir);

        // ライトのビュー行列（シーン中心を見る）
        Vector3 target = { 0.0f, 0.0f, 0.0f };
        Vector3 up = { 0.0f, 1.0f, 0.0f };
        Matrix4x4 lightView = MathCore::Matrix::LookAt(lightPos, target, up);

        // シャドウマップ用の正射影行列
        Matrix4x4 lightProjection = MathCore::Rendering::Orthographic(
            -config_.shadowOrthoSize, config_.shadowOrthoSize,  // 左、上
            config_.shadowOrthoSize, -config_.shadowOrthoSize,  // 右、下
            config_.shadowNearPlane, config_.shadowFarPlane      // 近平面、遠平面
        );

        // View * Projection
        Matrix4x4 lightViewProjection = MathCore::Matrix::Multiply(lightView, lightProjection);

        // RenderManagerに設定
        auto renderManager = ctx.engine->GetComponent<RenderManager>();
        if (renderManager) {
            renderManager->SetLightViewProjection(lightViewProjection);
        }
    }
}
