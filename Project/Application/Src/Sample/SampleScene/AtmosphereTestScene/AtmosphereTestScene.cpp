#include "pch.h"
#include "AtmosphereTestScene.h"

#include "Camera/CameraStructs.h"
#include "Camera/ICamera.h"
#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/Render/RenderDomainContext.h"

#include "Sample/TestGameObject/Primitive/CubeObject.h"
#include "Sample/TestGameObject/Primitive/PlaneObject.h"
#include "Sample/TestGameObject/SkyBox/SkyBoxObject.h"

using namespace CoreEngine;

void AtmosphereTestScene::OnInitialize()
{
    SetSceneName("AtmosphereTestScene");

    // ===== 太陽ライト =====
    // BaseScene::SetupLight() が生成した既定の DirectionalLight を太陽として使用する
    if (directionalLight_) {
        directionalLight_->isAtmosphereSun = true;

        const AtmosphereEditorSunSettings defaultSun{};
        directionalLight_->direction = AtmosphereEditorFacade::ComputeSunLightDirection(
            defaultSun.elevationDeg, defaultSun.azimuthDeg);
        directionalLight_->intensity = defaultSun.intensity;
    }

    // ===== 空（大気散乱モードの SkyBox） =====
    auto skyBox = CreateObject<SkyBoxObject>();
    skyBox->SetAtmosphereMode(true);
    skyBox->SetActive(true);

    // ===== 床（高度の目安になる基準面） =====
    auto ground = CreateObject<PlaneObject>(40.0f, 40.0f, 10u, 10u);
    ground->GetTransform().translate = { 0.0f, 0.0f, 0.0f };
    if (auto* mat = ground->GetModel()->GetMaterial()) {
        mat->SetColor({ 0.5f, 0.5f, 0.5f, 1.0f });
        mat->SetMetallic(0.0f);
        mat->SetRoughness(0.8f);
        mat->SetLightingEnabled(true);
    }
    ground->SetActive(true);

    // ===== 空気遠近感（Aerial Perspective）確認用の遠距離キューブ列 =====
    // 遠いものほど大気の霞で青白くなることを確認する。
    // 距離に比例して拡大し画面上でほぼ同じ大きさに見えるようにし、
    // 互いに重ならないよう方位を少しずつずらして横並びに配置する。
    constexpr float kApMarkerDistances[] = { 500.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f };
    constexpr int kApMarkerCount = static_cast<int>(std::size(kApMarkerDistances));
    for (int i = 0; i < kApMarkerCount; ++i) {
        const float distance = kApMarkerDistances[i];
        const float scale = distance / 50.0f; // 視角一定化のための距離比例スケール
        // 方位角オフセット [-14°, +14°] で横に展開
        const float azimuthOffsetRad = (static_cast<float>(i) - (kApMarkerCount - 1) * 0.5f) * 0.125f;
        auto marker = CreateObject<CubeObject>(1.0f);
        marker->GetTransform().translate = {
            distance * std::sin(azimuthOffsetRad),
            scale * 0.5f,
            distance * std::cos(azimuthOffsetRad) };
        marker->GetTransform().scale = { scale, scale, scale };
        if (auto* mat = marker->GetModel()->GetMaterial()) {
            mat->SetColor({ 0.35f, 0.3f, 0.28f, 1.0f });
            mat->SetMetallic(0.0f);
            mat->SetRoughness(0.9f);
            mat->SetLightingEnabled(true);
        }
        marker->SetActive(true);
    }

    // ===== カメラ =====
    // 空気遠近感の確認用に遠方（20km）のマーカーまで描画できるようファークリップを拡大
    if (ICamera* camera = GetGameViewCamera3D()) {
        CameraParameters params = camera->GetParameters();
        params.farClip = 50000.0f;
        camera->SetParameters(params);
    }

    // ===== エディタファサード =====
    editorFacade_.Initialize(*engine_);
}

void AtmosphereTestScene::OnUpdate()
{
    // AtmosphereManager へ太陽情報とカメラ高度を毎フレーム反映する
    auto* domainContext = engine_->GetRenderDomainContext();
    auto* atmosphereManager = domainContext ? domainContext->GetAtmosphereManager() : nullptr;
    if (atmosphereManager) {
        Vector3 cameraPosition{};
        Matrix4x4 viewMatrix = MathCore::Matrix::Identity();
        Matrix4x4 projMatrix = MathCore::Matrix::Identity();
        if (const ICamera* camera = GetGameViewCamera3D()) {
            cameraPosition = camera->GetPosition();
            viewMatrix = camera->GetViewMatrix();
            projMatrix = camera->GetProjectionMatrix();
        }
        atmosphereManager->Update(cameraPosition, viewMatrix, projMatrix,
                                  engine_->GetComponent<LightManager>());
    }

#ifdef USE_IMGUI
    editorFacade_.DrawImGui();
#endif
}

void AtmosphereTestScene::Draw()
{
    BaseScene::Draw();
}

void AtmosphereTestScene::Finalize()
{
    BaseScene::Finalize();
}
