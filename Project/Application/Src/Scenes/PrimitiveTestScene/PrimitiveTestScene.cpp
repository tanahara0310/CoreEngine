#include "pch.h"
#include "PrimitiveTestScene.h"

#ifdef _DEBUG
#include "Editor/Camera/CameraDebugUI.h"
#endif

#include "Graphics/Model/ModelManager.h"
#include "Input/KeyboardInput.h"
#include "Scene/SceneManager.h"
#include "Utility/Logger/Logger.h"

#include "Scenes/AtmosphereTestScene/AtmosphereEditorFacade.h"
#include "GameObjects/Primitive/PrimitiveSphereObject.h"
#include "GameObjects/Primitive/CubeObject.h"
#include "GameObjects/Primitive/RingObject.h"
#include "GameObjects/Primitive/CylinderObject.h"

using namespace CoreEngine;

void PrimitiveTestScene::OnInitialize()
{
    SetSceneName("PrimitiveTestScene");

    // ===== 太陽ライト =====
    // 空は BaseScene が既定で大気散乱モードの SkyBox（＋雲）を自動生成するため、
    // 以前の静的 HDR キューブマップ＋IBL セットアップは廃止した。
    // 各プリミティブの環境反射は大気の空キューブマップ（スペキュラIBL）が担う。
    // BaseScene::SetupLight() の既定値（天頂・intensity=1）は大気散乱が期待する
    // 輝度スケールと整合しないため明示的に上書きする（他の大気シーンと同じ定石）。
    if (directionalLight_) {
        directionalLight_->direction = AtmosphereEditorFacade::ComputeSunLightDirection(35.0f, 25.0f);
        // 空（大気・雲）の輝度スケールと、サーフェスの直接光は単位系が別なので分離して与える
        directionalLight_->atmosphereIntensity = 20.0f;
        directionalLight_->intensity = kAtmosphereSurfaceSunIntensity;
    }

    // ===== リング（Ring） =====
    // リングは XY 平面（垂直）に生成されるため、外周半径 1.5 より高い位置に置いて床に埋めない
    auto ring = CreateObject<RingObject>(1.5f, 0.5f, 64u, "gradationLine.png");
    ring->GetTransform().translate = { 0.0f, 1.8f, 0.0f };
    ring->GetTransform().scale = { 1.0f, 1.0f, 1.0f };
    ring->SetBlendMode(BlendMode::kBlendModeNormal);
    if (auto* mat = ring->GetModel()->GetMaterial()) {
        mat->SetMetallic(0.0f);
        mat->SetRoughness(0.5f);
        mat->SetLightingEnabled(true);
        // IBL はシーン側で自動有効化されるため個別設定は不要
    }
    ring->SetActive(true);

    // ===== シリンダー（Cylinder） =====
    // 高さ 2.2 の中心が原点なので y=1.1 で床にちょうど接地する
    auto cylinder = CreateObject<CylinderObject>(0.7f, 0.7f, 2.2f, 64u, "gradationLine.png");
    cylinder->GetTransform().translate = { 0.0f, 1.1f, 3.5f };
    cylinder->GetTransform().scale = { 1.0f, 1.0f, 1.0f };
    if (auto* mat = cylinder->GetModel()->GetMaterial()) {
        mat->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
        mat->SetMetallic(0.0f);
        mat->SetRoughness(0.35f);
        mat->SetLightingEnabled(true);
        // IBL はシーン側で自動有効化されるため個別設定は不要
    }
    cylinder->SetActive(true);

    // 地面は BaseScene が既定で生成する無限遠タイル床（y=0）を使う。
    // 以降のオブジェクトは全て床の上（y > 0）に配置する。

    // ===== 球体（PrimitiveSphere）× 5 ─ Roughness グラデーション =====
    constexpr int   kSphereCount = 5;
    constexpr float kSphereSpacing = 2.5f;
    const float     kSphereOriginX = -(kSphereCount - 1) * kSphereSpacing * 0.5f;

    for (int i = 0; i < kSphereCount; ++i) {
        float roughness = static_cast<float>(i) / (kSphereCount - 1);

        // 半径 0.8 なので y=0.8 で床に接地する
        auto sphere = CreateObject<PrimitiveSphereObject>(0.8f, 32u, 16u);
        sphere->GetTransform().translate = {
            kSphereOriginX + i * kSphereSpacing,
            0.8f,
            0.0f
        };
        if (auto* mat = sphere->GetModel()->GetMaterial()) {
            mat->SetColor({ 0.9f, 0.7f, 0.2f, 1.0f });
            mat->SetMetallic(1.0f);
            mat->SetRoughness(roughness);
            mat->SetLightingEnabled(true);
            // IBL はシーン側で自動有効化されるため個別設定は不要
            mat->SetIBLIntensity(1.0f);
        }
        sphere->SetActive(true);
    }

    // ===== 立方体（Cube）× 4 ─ 異なるカラー =====
    const Vector4 kCubeColors[4] = {
        { 0.9f, 0.2f, 0.2f, 1.0f },  // 赤
        { 0.2f, 0.8f, 0.3f, 1.0f },  // 緑
        { 0.2f, 0.4f, 0.9f, 1.0f },  // 青
        { 0.9f, 0.6f, 0.1f, 1.0f },  // 橙
    };

    constexpr float kCubeZ = -4.0f;
    constexpr float kCubeSpacing = 2.5f;
    const float     kCubeOriginX = -(3) * kCubeSpacing * 0.5f;

    for (int i = 0; i < 4; ++i) {
        // 一辺 1.2 の中心が原点なので y=0.6 で床に接地する
        auto cube = CreateObject<CubeObject>(1.2f);
        cube->GetTransform().translate = {
            kCubeOriginX + i * kCubeSpacing,
            0.6f,
            kCubeZ
        };
        cube->GetTransform().rotate = { 0.0f, 0.0f, 0.0f };
        if (auto* mat = cube->GetModel()->GetMaterial()) {
            mat->SetColor(kCubeColors[i]);
            mat->SetMetallic(0.0f);
            mat->SetRoughness(0.4f);
            mat->SetLightingEnabled(true);
            // IBL はシーン側で自動有効化されるため個別設定は不要
        }
        cube->SetActive(true);
    }
}

void PrimitiveTestScene::OnUpdate()
{
    auto keyboard = engine_->GetComponent<KeyboardInput>();
    if (!keyboard) {
        return;
    }

    // Tabキーでシーンをリスタート
    if (keyboard->IsKeyTriggered(DIK_TAB)) {
        if (sceneManager_) {
            sceneManager_->ChangeScene("PrimitiveTestScene");
        }
    }
}

void PrimitiveTestScene::Draw()
{
    BaseScene::Draw();
}

