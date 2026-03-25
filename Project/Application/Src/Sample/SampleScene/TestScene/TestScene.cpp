#include "EngineSystem/EngineSystem.h"

#ifdef _DEBUG
#include "Camera/Debug/Editor/CameraDebugUI.h"
#endif

#include "TestScene.h"
#include "WinApp/WinApp.h"
#include "Scene/SceneManager.h"
#include "Graphics/Render/RenderManager.h"
#include "Sample/TestGameObject/SkyBoxObject.h"
#include "Graphics/Texture/TextureManager.h"

#include <iostream>

using namespace CoreEngine::MathCore;

// アプリケーションの初期化

namespace CoreEngine
{
    void TestScene::Initialize(EngineSystem* engine)
    {
        BaseScene::Initialize(engine);
        SetSceneName("TestScene");

        ///========================================================
        // モデルの読み込みと初期化
        ///========================================================

        // コンポーネントを直接取得
        auto modelManager = engine_->GetComponent<ModelManager>();
        auto iblSystem = engine_->GetComponent<IBLSystem>();

        if (!modelManager || !iblSystem) {
            return; // 必須コンポーネントがない場合は終了
        }

        // ===== 環境マップテクスチャの読み込みと設定 =====
        auto& textureManager = TextureManager::GetInstance();

        // HDRファイルを読み込み（自動的にキューブマップDDSに変換される）
        TextureManager::LoadedTexture environmentMapTexture;
        environmentMapTexture = textureManager.Load("rogland_clear_night_4k.hdr");

        // ===== IBLシステムの初期化 =====
        IBLSystem::SetupParams iblParams;
        iblParams.environmentMap = environmentMapTexture.texture.Get();
        iblParams.environmentMapSRV = environmentMapTexture.gpuHandle;
        iblParams.environmentKey = "rogland_clear_night_4k.hdr";
        iblParams.irradianceSize = 128;
        iblParams.prefilteredSize = 256;
        iblParams.brdfLUTSize = 512;

        if (iblSystem->Setup(iblParams)) {
            Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Graphics, "{}", "IBL system initialized successfully");
        }
        else {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics, "{}", "Failed to initialize IBL system");
        }

        // SkyBoxの初期化
        auto skyBox = CreateObject<SkyBoxObject>();
        skyBox->SetTexture(environmentMapTexture);  // HDRから生成されたキューブマップを設定
        skyBox->SetActive(true);  // SkyBoxを表示

        ////// ===== sponzaモデルのみ配置 =====
        //auto sponza = CreateObject<ModelObject>("Sponza.gltf");
        //sponza->GetTransform().translate = { 0.0f, 0.0f, 0.0f };
        //sponza->GetTransform().scale = { 1.0f, 1.0f, 1.0f };
        //sponza->SetActive(false);

        // ===== ウォーキングモデル（PBR グリッドと重ならない位置に配置） =====
        auto walkModel = CreateObject<WalkModelObject>();
        walkModel->GetTransform().translate = { 25.0f, 0.0f, 0.0f };
        walkModel->GetTransform().scale = { 1.0f, 1.0f, 1.0f };
        walkModel->SetActive(true);

        // ===== PBR パラメータテスト用球体グリッド =====
        // 列（X 軸）: Roughness  0.0（左=鏡面） → 1.0（右=粗面）
        // 行（Y 軸）: Metallic   0.0（下=非金属） → 1.0（上=金属）
        //
        //  Metallic
        //  1.0 ┌──────────────────────────────────┐
        //      │  金属 × 各 Roughness              │
        //  0.5 │  半金属 × 各 Roughness            │
        //  0.0 └──────────────────────────────────┘
        //      0.0   0.17  0.33  0.5  0.67  0.83  1.0  Roughness

        constexpr int   kRoughnessSteps = 7;   // 列数（Roughness 軸）
        constexpr int   kMetallicSteps  = 7;   // 行数（Metallic 軸）
        constexpr float kSpacing        = 2.5f; // 球体間の間隔

        // グリッド原点（中央が座標原点になるよう計算）
        const float originX = -(kRoughnessSteps - 1) * kSpacing * 0.5f;
        const float originY = -(kMetallicSteps  - 1) * kSpacing * 0.5f;

        for (int row = 0; row < kMetallicSteps; ++row)
        {
            const float metallic = static_cast<float>(row) / static_cast<float>(kMetallicSteps - 1);

            for (int col = 0; col < kRoughnessSteps; ++col)
            {
                const float roughness = static_cast<float>(col) / static_cast<float>(kRoughnessSteps - 1);

                auto sphere = CreateObject<ModelObject>("sphere.obj");
                sphere->GetTransform().translate = {
                    originX + col * kSpacing,
                    originY + row * kSpacing,
                    0.0f
                };
                sphere->GetTransform().scale = { 1.0f, 1.0f, 1.0f };
                sphere->SetPBRParameters(metallic, roughness, 1.0f);
                sphere->SetIBLEnabled(true);
                sphere->SetIBLIntensity(1.0f);
                sphere->SetActive(true);
            }
        }
    }

    void TestScene::OnUpdate()
    {
        // KeyboardInput を直接取得
        auto keyboard = engine_->GetComponent<KeyboardInput>();
        if (!keyboard) {
            return;
        }

        // Tabキーでテストシーンをリスタート
        if (keyboard->IsKeyTriggered(DIK_TAB)) {
            if (sceneManager_) {
                sceneManager_->ChangeScene("TestScene");
            }
            return;
        }

        // ===== SkyBoxの回転を全球体のマテリアルに反映 =====
        SkyBoxObject* skyBox = nullptr;

        // SkyBoxオブジェクトを検索
        for (const auto& obj : gameObjectManager_.GetAllObjects()) {
            if (auto* sb = dynamic_cast<SkyBoxObject*>(obj.get())) {
                skyBox = sb;
                break;
            }
        }
    }

    void TestScene::Draw()
    {
        BaseScene::Draw();
    }


    void TestScene::Finalize()
    {
    }
}


