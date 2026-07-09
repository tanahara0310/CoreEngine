#include "pch.h"
#include "VolumetricCloudTestScene.h"

#include "Camera/CameraStructs.h"
#include "Camera/ICamera.h"

#include "Sample/SampleScene/AtmosphereTestScene/AtmosphereEditorFacade.h"
#include "Sample/TestGameObject/Primitive/PlaneObject.h"

using namespace CoreEngine;

void VolumetricCloudTestScene::OnInitialize()
{
    SetSceneName("VolumetricCloudTestScene");

    // ===== 太陽ライト =====
    // BaseScene::SetupLight() の既定値（天頂・intensity=1）は大気散乱が期待する
    // 輝度スケール（intensity=20 目安）と整合しない。明示的に上書きしないと
    // 空が暗い紺色のまま・雲も真っ黒になる（WaterTestScene で実際に踏んだ不具合）。
    if (directionalLight_) {
        directionalLight_->direction = AtmosphereEditorFacade::ComputeSunLightDirection(35.0f, 25.0f);
        directionalLight_->intensity = 20.0f;
    }

    // 空は BaseScene が既定で大気散乱モードの SkyBox を自動生成する。
    // 雲も既定 enabled=true のため、SkyBox を自前生成・テクスチャ設定しなければ
    // 大気散乱モードのまま維持され、雲も自動的に有効になる。

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

    // ===== カメラ =====
    // 雲層はマーチ最大距離 30km まで広がるため、既定 farClip（1000m）では
    // 途中で切れてしまう。AtmosphereTestScene と同様に拡大する。
    if (ICamera* camera = GetGameViewCamera3D()) {
        CameraParameters params = camera->GetParameters();
        params.farClip = 50000.0f;
        camera->SetParameters(params);
    }

    // ===== エディタファサード =====
    editorFacade_.Initialize(*engine_);
}

void VolumetricCloudTestScene::OnUpdate()
{
    // VolumetricCloudManager への太陽・カメラ情報の反映は
    // BaseScene::UpdateAtmosphere() が大気散乱の直後に毎フレーム行う
#ifdef USE_IMGUI
    editorFacade_.DrawImGui();
#endif
}

void VolumetricCloudTestScene::Draw()
{
    BaseScene::Draw();
}

void VolumetricCloudTestScene::Finalize()
{
    BaseScene::Finalize();
}
