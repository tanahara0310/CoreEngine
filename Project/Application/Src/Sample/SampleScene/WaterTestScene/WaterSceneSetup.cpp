#include "pch.h"
#include "WaterSceneSetup.h"

#include "WaterTestScene.h"
#include "EngineSystem/EngineSystem.h"

using namespace CoreEngine;

WaterSceneObjects WaterSceneSetup::SetupScene(WaterTestScene& scene, [[maybe_unused]] EngineSystem& engine) {
    WaterSceneObjects sceneObjects{};

    // 背景・IBL 環境マップは明示指定しない。BaseScene::SetupDefaultSky() が
    // OnInitialize() 完了後に既定背景（大気散乱モード SkyBox）を自動生成し、
    // BaseScene::SetupLight() が生成した isAtmosphereSun 付きライトと連動して
    // AtmosphereManager を毎フレーム更新する。
    // これにより水面の Planar Reflection（ReflectionView）が大気散乱の空を
    // そのまま映し込み、コースティクス（RTWaterCaustics 等）が参照する太陽も
    // 同一の isAtmosphereSun ライトのため、反射・コースティクス・空が常に
    // 同じ太陽方向/色で整合する。

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
    // 大気散乱モードでは静的環境マップの IBL を使わない（既定の ShadingMode::PBR の
    // ハーフランバートアンビエントで代替）。水面の鏡面反射は Planar Reflection
    // （ReflectionView が大気散乱の空を含めて描画した SceneColor）で賄うため、
    // ここで静的 IBL を有効にすると大気の空と映り込みが食い違う。
    material->SetIBLEnabled(false);
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
    // 水面と同じ理由で静的 IBL は使わず、大気散乱と整合する直接光ベースの PBR とする。
    groundObject->SetIBLEnabled(false);
    groundObject->SetActive(true);
}
