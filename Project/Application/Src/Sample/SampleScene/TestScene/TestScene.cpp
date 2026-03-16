#include "EngineSystem/EngineSystem.h"

#ifdef _DEBUG
#include "Camera/Debug/Editor/CameraDebugUI.h"
#endif

#include "TestScene.h"
#include "WinApp/WinApp.h"
#include "Scene/SceneManager.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/Model/ModelRenderer.h"
#include "Graphics/Render/Model/SkinnedModelRenderer.h"
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
        auto dxCommon = engine_->GetComponent<DirectXCommon>();
        auto modelManager = engine_->GetComponent<ModelManager>();
        auto renderManager = engine_->GetComponent<RenderManager>();

        if (!dxCommon || !modelManager || !renderManager) {
            return; // 必須コンポーネントがない場合は終了
        }

        // ===== 環境マップテクスチャの読み込みと設定 =====
        auto& textureManager = TextureManager::GetInstance();

        // HDRファイルを読み込み（自動的にキューブマップDDSに変換される）
        TextureManager::LoadedTexture environmentMapTexture;
        environmentMapTexture = textureManager.Load("kloppenheim_06_puresky_4k.hdr");

        // ===== IBLシステムの初期化 =====
        auto* iblGenerator = engine_->GetComponent<IBLGenerator>();
        if (iblGenerator) {
            // IBLManagerを使用してIBLシステムを初期化
            iblManager_ = std::make_unique<IBLManager>();

            IBLManager::InitParams iblParams;
            iblParams.environmentMap = environmentMapTexture.texture.Get();
            iblParams.irradianceSize = 128;
            iblParams.prefilteredSize = 256;
            iblParams.brdfLUTSize = 512;

            if (iblManager_->Initialize(dxCommon, iblGenerator, iblParams)) {
                Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Graphics, "{}", "IBL system initialized successfully");
            } else {
                Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics, "{}", "Failed to initialize IBL system");
            }
        }

        // ===== レンダラーに環境マップとIBLリソースを設定 =====

        // SkinnedModelRendererに設定
        auto* skinnedModelRenderer = renderManager->GetRenderer(RenderPassType::SkinnedModel);
        if (skinnedModelRenderer) {
            static_cast<SkinnedModelRenderer*>(skinnedModelRenderer)->SetEnvironmentMap(environmentMapTexture.gpuHandle);

            if (iblManager_ && iblManager_->IsInitialized()) {
                iblManager_->SetToSkinnedModelRenderer(static_cast<SkinnedModelRenderer*>(skinnedModelRenderer));
            }
        }

        // ModelRendererに設定
        auto* modelRenderer = renderManager->GetRenderer(RenderPassType::Model);
        if (modelRenderer) {
            static_cast<ModelRenderer*>(modelRenderer)->SetEnvironmentMap(environmentMapTexture.gpuHandle);


            if (iblManager_ && iblManager_->IsInitialized()) {
                iblManager_->SetToModelRenderer(static_cast<ModelRenderer*>(modelRenderer));
            }
        }

        // SkyBoxの初期化
        auto skyBox = CreateObject<SkyBoxObject>();
        skyBox->Initialize();
        skyBox->SetTexture(environmentMapTexture);  // HDRから生成されたキューブマップを設定
        skyBox->SetActive(true);  // SkyBoxを表示

        // ===== sponzaモデルのみ配置 =====
        auto sponza = CreateObject<ModelObject>();
        sponza->Initialize("C:/CoreEngine/Project/Engine/Assets/Model/sponza/Sponza.gltf");
        sponza->GetTransform().translate = { 0.0f, 0.0f, 0.0f };
        sponza->GetTransform().scale = { 1.0f, 1.0f, 1.0f };
        sponza->SetActive(true);
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


