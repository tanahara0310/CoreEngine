#include "pch.h"
#include "PrimitiveTestScene.h"

#ifdef _DEBUG
#include "Editor/Camera/CameraDebugUI.h"
#endif

#include "Graphics/Model/ModelManager.h"
#include "Graphics/Texture/TextureManager.h"
#include "Graphics/IBL/IBLSystem.h"
#include "Input/KeyboardInput.h"
#include "Scene/SceneManager.h"
#include "Utility/Logger/Logger.h"

#include "GameObjects/SkyBox/SkyBoxObject.h"
#include "GameObjects/Primitive/PrimitiveSphereObject.h"
#include "GameObjects/Primitive/CubeObject.h"
#include "GameObjects/Primitive/RingObject.h"
#include "GameObjects/Primitive/CylinderObject.h"

using namespace CoreEngine;

void PrimitiveTestScene::OnInitialize()
{
    SetSceneName("PrimitiveTestScene");

    auto iblSystem = engine_->GetComponent<IBLSystem>();
    if (!iblSystem) {
        return;
    }

    // ===== 環境マップ・IBL =====
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
        mat->SetIBLEnabled(true);
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
        mat->SetIBLEnabled(true);
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
            mat->SetIBLEnabled(true);
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
            mat->SetIBLEnabled(true);
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

