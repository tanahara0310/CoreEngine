#include "pch.h"
#include "AtmosphereTestScene.h"

#include "Camera/CameraStructs.h"
#include "Camera/ICamera.h"

#include "Editor/Environment/AtmosphereEditor.h"
#include "GameObjects/Primitive/CubeObject.h"

using namespace CoreEngine;

void AtmosphereTestScene::OnInitialize()
{
    SetSceneName("AtmosphereTestScene");

    // ===== 太陽ライト =====
    // BaseScene::SetupLight() が生成した既定の DirectionalLight（isAtmosphereSun 付与済み）を
    // 本シーンの既定仰角・方位へ調整する
    if (directionalLight_) {
        const AtmosphereEditorSunSettings defaultSun{};
        directionalLight_->direction = AtmosphereEditor::ComputeSunLightDirection(
            defaultSun.elevationDeg, defaultSun.azimuthDeg);
        // 太陽UIの intensity は「空の輝度スケール」。サーフェスの直接光は別単位で与える。
        directionalLight_->atmosphereIntensity = defaultSun.intensity;
        directionalLight_->intensity = kAtmosphereSunIlluminanceLux;
    }

    // 空は BaseScene が既定で大気散乱モードの SkyBox を自動生成するためここでは何もしない

    // 床（高度の目安になる基準面）は BaseScene が既定で生成する無限遠タイル床（y=0）を使う

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

    // 大気の編集 UI はエンジン常駐の AtmosphereEditor が全シーン共通で登録済み
}

void AtmosphereTestScene::OnUpdate()
{
    // AtmosphereManager への太陽・カメラ情報の反映は BaseScene::UpdateAtmosphere() が毎フレーム行う
}

void AtmosphereTestScene::Draw()
{
    BaseScene::Draw();
}

