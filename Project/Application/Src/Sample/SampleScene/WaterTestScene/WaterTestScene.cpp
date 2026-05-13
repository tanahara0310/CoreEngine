#include "WaterTestScene.h"

#include "Graphics/IBL/IBLSystem.h"
#include "Graphics/Texture/TextureManager.h"
#include "Scene/SceneManager.h"
#include "Sample/TestGameObject/SkyBox/SkyBoxObject.h"
#include "Sample/TestGameObject/Primitive/WaterPlaneObject.h"
#include "Utility/FrameRate/FrameRateController.h"

using namespace CoreEngine;

void WaterTestScene::OnInitialize() {
    SetSceneName("WaterTestScene");

    // ===== IBL セットアップ =====
    auto iblSystem = engine_->GetComponent<IBLSystem>();
    if (!iblSystem) {
        return;
    }

    auto& textureManager = TextureManager::GetInstance();
    auto environmentMapTexture = textureManager.Load("kloppenheim_06_puresky_4k.hdr");

    IBLSystem::SetupParams iblParams;
    iblParams.environmentMap = environmentMapTexture.texture.Get();
    iblParams.environmentMapSRV = environmentMapTexture.gpuHandle;
    iblParams.environmentKey = "kloppenheim_06_puresky_4k.hdr";
    iblParams.irradianceSize = 128;
    iblParams.prefilteredSize = 256;
    iblParams.brdfLUTSize = 512;
    iblSystem->Setup(iblParams);

    // ===== SkyBox =====
    auto skyBox = CreateObject<SkyBoxObject>();
    skyBox->SetTexture(environmentMapTexture);
    skyBox->SetActive(true);

    // ===== 水面グリッドメッシュ =====
    // サイズ 100 × 100、64×64 分割
    // 分割数が多いほど後のステップ（Gerstner Wave）で波の表現が細かくなる
    waterPlane_ = CreateObject<WaterPlaneObject>(100.0f, 64, "waterAlbedo.jpg");
    waterPlane_->GetTransform().translate = { 0.0f, 0.0f, 0.0f };
    waterPlane_->GetTransform().scale = { 1.0f, 1.0f, 1.0f };

    if (auto* mat = waterPlane_->GetModel()->GetMaterial()) {
        // Step 2: UV スクロールで水面の流れを表現
        mat->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
        mat->SetMetallic(0.0f);
        mat->SetRoughness(0.05f);
        mat->SetLightingEnabled(true);
        mat->SetIBLEnabled(true);
    }

    // ノーマルマップを設定（Initialize後に呼ぶ）
    waterPlane_->SetNormalMapTextureName("waterNormal.jpg");

    // UV スクロール速度とタイリングを設定
    // scrollSpeed: U方向に 0.03 UV/秒、V方向に 0.01 UV/秒 でゆっくり流れる
    waterPlane_->SetScrollSpeed({ 0.03f, 0.01f });
    waterPlane_->SetUVTiling({ 4.0f, 4.0f });
    waterPlane_->SetActive(true);
}

void WaterTestScene::OnUpdate() {
    // フレーム時間を取得して UV スクロールを更新
    auto* frameRate = engine_->GetComponent<FrameRateController>();
    if (waterPlane_ && frameRate) {
        waterPlane_->UpdateUVScroll(frameRate->GetDeltaTime());
    }
}

void WaterTestScene::Draw() {
    BaseScene::Draw();
}

void WaterTestScene::Finalize() {
    BaseScene::Finalize();
}
