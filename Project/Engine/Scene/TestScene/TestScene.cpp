#include <EngineSystem.h>

#ifdef _DEBUG
#include "Engine/Camera/Debug/CameraDebugUI.h"
#endif

#include "TestScene.h"
#include "WinApp/WinApp.h"
#include "Scene/SceneManager.h"
#include "Engine/Graphics/Render/RenderManager.h"
#include "Engine/Graphics/Render/Model/ModelRenderer.h"
#include "Engine/Graphics/Render/Model/SkinnedModelRenderer.h"
#include "Engine/Graphics/TextureManager.h"
#include "Engine/TestGameObject/ModelObject.h"

#include <iostream>

using namespace CoreEngine::MathCore;

// アプリケーションの初期化

namespace CoreEngine
{
    void TestScene::Initialize(EngineSystem* engine)
    {

        BaseScene::Initialize(engine);

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
        environmentMapTexture = textureManager.Load("Texture/rostock_laage_airport_4k.dds");

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
                Logger::GetInstance().Log("IBL system initialized successfully",
                    LogLevel::INFO, LogCategory::Graphics);
            } else {
                Logger::GetInstance().Log("Failed to initialize IBL system",
                    LogLevel::Error, LogCategory::Graphics);
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


        // ===== glTF PBRテストモデルの読み込み =====
        // A Beautiful Game（チェスのモデル）を中央に配置

        auto chessModel = CreateObject<ModelObject>();
        chessModel->Initialize("SampleAssets/PblTestModel/ABeautifulGame.gltf");
        chessModel->SetPBREnabled(true);
        chessModel->SetMaterialColor({ 1.0f, 1.0f, 1.0f, 1.0f });
        chessModel->SetEnvironmentMapEnabled(true);
        chessModel->SetEnvironmentMapIntensity(0.5f);
        chessModel->SetIBLEnabled(true);
        chessModel->SetIBLIntensity(1.0f);
        chessModel->SetPBRParameters(0.5f, 0.3f, 1.0f); // Metallic: 0.5, Roughness: 0.3, AO: 1.0
        chessModel->GetTransform().translate = { 0.0f, 0.0f, 0.0f };
        chessModel->GetTransform().scale = { 20.0f, 20.0f, 20.0f };
        chessModel->SetActive(true);


        // SkyBoxの初期化
        auto skyBox = CreateObject<SkyBoxObject>();
        skyBox->Initialize();
        skyBox->SetTexture(environmentMapTexture);  // HDRから生成されたキューブマップを設定
        skyBox->SetActive(true);  // SkyBoxを表示
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

        // SkyBoxの回転を全球体とモデルのマテリアルに反映
        if (skyBox) {
            float skyBoxRotationY = skyBox->GetRotationY();

            for (const auto& obj : gameObjectManager_.GetAllObjects()) {
                if (auto* sphere = dynamic_cast<SphereObject*>(obj.get())) {
                    sphere->SetEnvironmentRotationY(skyBoxRotationY);
                }
                else if (auto* model = dynamic_cast<ModelObject*>(obj.get())) {
                    model->SetEnvironmentRotationY(skyBoxRotationY);
                }
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
