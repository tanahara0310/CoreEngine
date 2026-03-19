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
        skyBox->Initialize();
        skyBox->SetTexture(environmentMapTexture);  // HDRから生成されたキューブマップを設定
        skyBox->SetActive(true);  // SkyBoxを表示

        //// ===== sponzaモデルのみ配置 =====
        auto sponza = CreateObject<ModelObject>();
        sponza->Initialize("Sponza.gltf");
        sponza->GetTransform().translate = { 0.0f, 0.0f, 0.0f };
        sponza->GetTransform().scale = { 1.0f, 1.0f, 1.0f };
        sponza->SetActive(false);
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


