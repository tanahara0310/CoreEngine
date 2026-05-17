#include "PrimitiveTestScene.h"

#ifdef _DEBUG
#include "Camera/Debug/Editor/CameraDebugUI.h"
#endif

#include "Graphics/Model/ModelManager.h"
#include "Graphics/Texture/TextureManager.h"
#include "Graphics/IBL/IBLSystem.h"
#include "Input/KeyboardInput.h"
#include "Scene/SceneManager.h"
#include "Utility/Logger/Logger.h"

#include "Sample/TestGameObject/SkyBox/SkyBoxObject.h"
#include "Sample/TestGameObject/Primitive/PlaneObject.h"
#include "Sample/TestGameObject/Primitive/PrimitiveSphereObject.h"
#include "Sample/TestGameObject/Primitive/CubeObject.h"

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

    // ===== 地面（Plane） =====
    // 幅20・奥行20、10×10分割
    auto ground = CreateObject<PlaneObject>(20.0f, 20.0f, 10u, 10u);
    ground->GetTransform().translate = { 0.0f, -1.0f, 0.0f };
    ground->GetTransform().scale = { 1.0f, 1.0f, 1.0f };
    if (auto* mat = ground->GetModel()->GetMaterial()) {
        mat->SetColor({ 0.6f, 0.6f, 0.6f, 1.0f });
        mat->SetMetallic(0.0f);
        mat->SetRoughness(0.8f);
        mat->SetLightingEnabled(true);
        mat->SetIBLEnabled(true);
    }
    ground->SetActive(true);

    // ===== 球体（PrimitiveSphere）× 5 ─ Roughness グラデーション =====
    constexpr int   kSphereCount = 5;
    constexpr float kSphereSpacing = 2.5f;
    const float     kSphereOriginX = -(kSphereCount - 1) * kSphereSpacing * 0.5f;

    for (int i = 0; i < kSphereCount; ++i) {
        float roughness = static_cast<float>(i) / (kSphereCount - 1);

        auto sphere = CreateObject<PrimitiveSphereObject>(0.8f, 32u, 16u);
        sphere->GetTransform().translate = {
            kSphereOriginX + i * kSphereSpacing,
            0.0f,
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
        auto cube = CreateObject<CubeObject>(1.2f);
        cube->GetTransform().translate = {
            kCubeOriginX + i * kCubeSpacing,
            -0.4f,
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

void PrimitiveTestScene::Finalize()
{
    BaseScene::Finalize();
}
