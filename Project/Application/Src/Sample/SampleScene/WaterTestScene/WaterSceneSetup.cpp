#include "pch.h"
#include "WaterSceneSetup.h"

#include "WaterTestScene.h"
#include "EngineSystem/EngineSystem.h"
#include "Graphics/IBL/IBLSystem.h"
#include "Graphics/Texture/TextureManager.h"
#include "Sample/TestGameObject/SkyBox/SkyBoxObject.h"

using namespace CoreEngine;

namespace {
    /// @brief WaterTestScene で使用する IBL 環境マップ名
    constexpr const char* kWaterSceneEnvironmentMap = "kloppenheim_06_puresky_4k.hdr";
}

WaterSceneObjects WaterSceneSetup::SetupScene(WaterTestScene& scene, EngineSystem& engine) {
    WaterSceneObjects sceneObjects{};

    if (auto* iblSystem = engine.GetComponent<IBLSystem>()) {
        // IBL 用の環境マップを読み込み、Water シーン用のライティング環境を初期化する
        auto& textureManager = TextureManager::GetInstance();
        auto environmentMapTexture = textureManager.Load(kWaterSceneEnvironmentMap);

        IBLSystem::SetupParams iblParams;
        iblParams.environmentMap = environmentMapTexture.texture.Get();
        iblParams.environmentMapSRV = environmentMapTexture.gpuHandle;
        iblParams.environmentKey = kWaterSceneEnvironmentMap;
        iblParams.irradianceSize = 128;
        iblParams.prefilteredSize = 256;
        iblParams.brdfLUTSize = 512;
        iblSystem->Setup(iblParams);

        // SkyBox を生成して現在の環境マップを可視化する
        auto skyBox = scene.CreateWaterSceneObject<SkyBoxObject>();
        skyBox->SetTexture(environmentMapTexture);
        skyBox->SetActive(true);
    }

    // Water 描画に必要な主要オブジェクトを順に構築する
    sceneObjects.waterPlane = CreateWaterPlane(scene);
    sceneObjects.groundObject = CreateGroundObject(scene);

    ConfigureWaterMaterial(sceneObjects.waterPlane);
    ConfigureGroundObject(sceneObjects.groundObject);
    return sceneObjects;
}

WaterPlaneObject* WaterSceneSetup::CreateWaterPlane(WaterTestScene& scene) {
    // 水面グリッドを生成し、水面描画用の基本状態を設定する
    WaterPlaneObject* waterPlane = scene.CreateWaterSceneObject<WaterPlaneObject>(100.0f, 128, true);
    if (!waterPlane) {
        return nullptr;
    }

    waterPlane->GetTransform().translate = { 0.0f, 0.0f, 0.0f };
    waterPlane->GetTransform().scale = { 1.0f, 1.0f, 1.0f };
    waterPlane->SetBlendMode(BlendMode::kBlendModeNormal);
    waterPlane->SetScrollSpeed({ 0.03f, 0.01f });
    waterPlane->SetUVTiling({ 4.0f, 4.0f });
    waterPlane->SetActive(true);
    return waterPlane;
}

ModelObject* WaterSceneSetup::CreateGroundObject(WaterTestScene& scene) {
    // 水中地形モデルを生成し、水面直下へ配置する
    return scene.CreateWaterSceneObject<ModelObject>("pool.gltf");
}

void WaterSceneSetup::ConfigureWaterMaterial(WaterPlaneObject* waterPlane) {
    if (!waterPlane || !waterPlane->GetModel() || !waterPlane->GetModel()->GetMaterial()) {
        return;
    }

    auto* material = waterPlane->GetModel()->GetMaterial();

    // 水面らしい鏡面的な PBR 初期値を設定する
    material->SetColor({ 0.04f, 0.18f, 0.28f, 0.85f });
    material->SetMetallic(0.0f);
    material->SetRoughness(0.04f);
    material->SetLightingEnabled(true);
    material->SetIBLEnabled(true);
    material->SetNormalMapEnabled(false);
    material->SetMetallicMapEnabled(false);
    material->SetRoughnessMapEnabled(false);
    material->SetAOMapEnabled(false);
}

void WaterSceneSetup::ConfigureGroundObject(ModelObject* groundObject) {
    if (!groundObject) {
        return;
    }

    // 水中地形モデルを水面直下へ配置し、PBR 描画を有効化する
    groundObject->GetTransform().translate = { 0.0f, -0.1f, 0.0f };
    groundObject->GetTransform().scale = { 1.0f, 1.0f, 1.0f };
    groundObject->SetPBRTextureMapsEnabled(true, true, true, true);
    groundObject->SetIBLEnabled(true);
    groundObject->SetActive(true);
}
