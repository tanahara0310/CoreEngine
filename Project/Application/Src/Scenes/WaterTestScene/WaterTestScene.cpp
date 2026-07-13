#include "pch.h"
#include "WaterTestScene.h"

#include "Graphics/Render/RenderTarget/RenderTargetNames.h"
#include "Utility/FrameRate/FrameRateController.h"
#include "Math/MathCore.h"
#include <cmath>

using namespace CoreEngine;

namespace {
    constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

    // 太陽高度角・方位角からライト方向（太陽 → 地表への進行方向）を計算する。
    // AtmosphereEditorFacade::ComputeSunLightDirection と同じ規約（elevation=90°で天頂）。
    Vector3 ComputeSunLightDirection(float elevationDeg, float azimuthDeg) {
        const float elevation = elevationDeg * kDegToRad;
        const float azimuth = azimuthDeg * kDegToRad;
        const Vector3 toSun = {
            std::cos(elevation) * std::sin(azimuth),
            std::sin(elevation),
            std::cos(elevation) * std::cos(azimuth),
        };
        return MathCore::Vector::Normalize({ -toSun.x, -toSun.y, -toSun.z });
    }
}

void WaterTestScene::OnInitialize() {
    SetSceneName("WaterTestScene");

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
        directionalLight_->intensity = kAtmosphereSurfaceSunIntensity;
    }

    waterController_.Initialize(*this, *engine_);

    // プール一式は既定の無限遠タイル床（y=0）の上へ 6m 持ち上げてある。
    // 従来と同じ構図になるようカメラも同じだけ持ち上げる。
    SetReleaseCameraTransform({ 0.0f, 6.0f, -30.0f });
}

void WaterTestScene::OnUpdate() {
    auto* frameRate = engine_->GetComponent<FrameRateController>();
    const float deltaTime = frameRate ? frameRate->GetDeltaTime() : (1.0f / 60.0f);
    waterController_.Update(*engine_, deltaTime);
}

void WaterTestScene::Draw() {
    BaseScene::Draw();
}

std::vector<RenderViewRequest> WaterTestScene::BuildRenderViewRequests()
{
    std::vector<RenderViewRequest> requests;

    WaterPlaneObject* waterPlane = waterController_.GetWaterPlane();
    if (!waterPlane) {
        return requests;
    }

    ICamera* mainCamera = GetGameViewCamera3D();
    const float planeHeight = waterController_.GetWaterHeight();

    RenderViewRequest request{};
    request.isEnabled = true;
    request.name = "WaterReflection";
    request.viewSettings.viewType = RenderViewType::ReflectionView;
    request.viewSettings.enableSSAO = false;
    request.viewSettings.enableRTShadow = false;
    request.viewSettings.enablePostEffect = false;
    request.viewSettings.enableBackBuffer = false;
    request.viewSettings.sceneColorTargetName = RenderTargetNames::ReflectionView;
    request.beforeExecute = [this, mainCamera, planeHeight]() {
        SetupWaterReflectionView(mainCamera, planeHeight);
    };
    request.afterExecute = [this, mainCamera]() {
        RestoreWaterReflectionView(mainCamera);
    };
    request.completionCallback = [this](const RenderViewResult& result) {
        ApplyWaterRenderViewResult(result);
    };

    requests.push_back(std::move(request));
    return requests;
}

void WaterTestScene::SetupWaterReflectionView(ICamera* mainCamera, float planeHeight)
{
    reflectionPass_.SetupReflectionCamera(mainCamera, planeHeight);

    if (WaterPlaneObject* waterPlane = waterController_.GetWaterPlane()) {
        waterPlane->SetClipPlane(reflectionPass_.GetClipPlane(), true);
        waterPlane->UpdateFrameConstants();
    }
}

void WaterTestScene::RestoreWaterReflectionView(ICamera* mainCamera){
    reflectionPass_.RestoreMainCamera(mainCamera);
}

void WaterTestScene::ApplyWaterRenderViewResult(const RenderViewResult& result)
{
    WaterPlaneObject* waterPlane = waterController_.GetWaterPlane();
    if (!waterPlane) {
        return;
    }

    waterController_.ApplyWaterRenderViewResult(result);
    waterPlane->SetClipPlane(reflectionPass_.GetClipPlane(), false);
    waterPlane->UpdateFrameConstants();
}

const WaterSurfaceData* WaterTestScene::GetWaterRefractionSurfaceData() const
{
    return waterController_.GetWaterRefractionSurfaceData();
}

