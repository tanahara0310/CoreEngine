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
    // 水面反射（RTWaterReflectionPass）もコースティクス（RTWaterCaustics）も
    // 同一の isAtmosphereSun ライトを参照するため、反射・コースティクス・空が
    // 常に同じ太陽方向/色で整合する。

    // Water 描画に必要な主要オブジェクトを順に構築する
    sceneObjects.waterPlane = CreateWaterPlane(scene);

    ConfigureWaterMaterial(sceneObjects.waterPlane);
    return sceneObjects;
}

WaterPlaneObject* WaterSceneSetup::CreateWaterPlane(WaterTestScene& scene) {
    // 水面グリッドを生成し、水面描画用の基本状態を設定する。
    // FFT カスケード（パッチ長 340/89/23m）の波形をジオメトリで解像するには
    // 頂点間隔がカスケード波長より十分細かい必要がある。
    // ローカル 100m × 分割 256 × スケール 40 = 一辺 4km / 頂点間隔 15.6m とする。
    // （カメラ追従はギズモ操作を阻害するため撤去済み。水域は固定タイル）
    // スケールを極端に上げると頂点間隔が波長を超えて
    // 「平面全体が上下するだけ」の見た目に退化するので注意。
    WaterPlaneObject* waterPlane = scene.CreateWaterSceneObject<WaterPlaneObject>(100.0f, 256, true);
    if (!waterPlane) {
        return nullptr;
    }

    // 既定の無限遠タイル床（y=0）の高さに合わせて水面を配置する
    waterPlane->GetTransform().translate = { 0.0f, 0.0f, 0.0f };
    waterPlane->GetTransform().scale = { 40.0f, 1.0f, 40.0f };
    waterPlane->SetBlendMode(BlendMode::kBlendModeNormal);
    waterPlane->SetScrollSpeed({ 0.03f, 0.01f });
    waterPlane->SetUVTiling({ 4.0f, 4.0f });
    waterPlane->SetActive(true);
    return waterPlane;
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
    // 水面の鏡面反射は RTWaterReflectionPass（DXR）と空環境キューブマップで賄うため、
    // ここで静的環境マップの IBL を有効にすると大気の空と映り込みが食い違う。
    // 強度 0 でオプトアウトする。
    material->SetIBLIntensity(0.0f);
    material->SetNormalMapEnabled(false);
}
