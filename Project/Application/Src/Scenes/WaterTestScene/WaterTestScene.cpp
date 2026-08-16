#include "pch.h"
#include "WaterTestScene.h"

#include "Graphics/Water/Render/WaterRenderFeature.h"
#include "Math/MathCore.h"
#include <cmath>
#include <memory>

using namespace CoreEngine;

namespace {
    constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

    // 太陽高度角・方位角からライト方向（太陽 → 地表への進行方向）を計算する。
    // AtmosphereEditor::ComputeSunLightDirection と同じ規約（elevation=90°で天頂）。
    Vector3 ComputeSunLightDirection(float elevationDeg, float azimuthDeg) {
        const float elevation = elevationDeg * kDegToRad;
        const float azimuth = azimuthDeg * kDegToRad;
        const Vector3 toSun = {
            std::cos(elevation) * std::sin(azimuth),
            std::sin(elevation),
            std::cos(elevation) * std::cos(azimuth),
        };
        return CoreEngine::Normalize(-toSun);
    }
}

void WaterTestScene::OnInitialize() {
    SetSceneName("WaterTestScene");

    // 既定床は使わない。このシーンは海面（y≈0）と島の地形を自前で持っているため、
    // y=0 に平らな床を置くと水面と Z 争いを起こし、海底の地形とも二重になる。
    SetDefaultGroundEnabled(false);

    // ===== 太陽ライト =====
    // BaseScene::SetupLight() の既定値（天頂・intensity=1）は通常の直接光用の目安であり、
    // 大気散乱が期待する輝度スケール（AtmosphereEditorSunSettings 既定値は intensity=20）とは
    // 整合しない。既定背景が大気散乱モードになったため、AtmosphereTestScene と同じ考え方で
    // 見栄えの良い太陽高度・強度に調整し、水面反射／コースティクスが参照する太陽と
    // 空に映る太陽を一致させる。
    if (directionalLight_) {
        directionalLight_->direction = ComputeSunLightDirection(35.0f, 25.0f);
        // 空（大気・雲）の輝度スケールと、サーフェスの直接光は単位系が別なので分離して与える。
        // 両方に 20 を入れると床のような明るいアルベドが ACES の飽和域に入り真っ白になる。
        directionalLight_->atmosphereIntensity = 20.0f;
        directionalLight_->intensity = kAtmosphereSunIlluminanceLux;
    }

    // 水面一式（水面オブジェクト・波シミュレーション・リソース結線）は Feature が持つ。
    // シーン側は登録するだけで、以降の毎フレーム処理に手を入れる必要がない。
    auto* waterFeature = static_cast<WaterRenderFeature*>(
        AddFeature(std::make_unique<WaterRenderFeature>()));
    waterController_.Initialize(waterFeature, *engine_);

    // 起動時のリリースカメラはシーン全体（水面・地形・配置物）を俯瞰する構図にする。
    // 位置 (0, 60, -90) から約 35° 見下ろすと、原点付近（海面 y≈0）が画角中央に入る。
    SetReleaseCameraTransform({ 0.0f, 60.0f, -90.0f }, { 35.0f * kDegToRad, 0.0f, 0.0f });
}

void WaterTestScene::Draw() {
    BaseScene::Draw();
}

void WaterTestScene::OnFinalize() {
    // WaterRenderFeature の所有者は BaseScene（この直後に features_ が破棄される）。
    // UI が Feature ポインタを持ったままにならないよう、ここで先に切る。
    waterController_.Shutdown();
}

std::vector<RenderViewRequest> WaterTestScene::BuildRenderViewRequests()
{
    // 鏡像カメラによる平面反射ビューは廃止した。
    // 水面反射は DXR（RTWaterReflectionPass）へ置き換え済みで、シーン全体を
    // もう一周描画する反射ビューは不要になった（反射コストがシーン複雑度から独立化）。
    // 反射ビューを発行しないことで、GBuffer/FFTOcean/ライティングの二重実行が消える。
    return {};
}


