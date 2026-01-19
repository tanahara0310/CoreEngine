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
	auto environmentMapTexture = textureManager.Load("SampleAssets/SkyBox/rostock_laage_airport_4k.dds");

	// SkinnedModelRendererに環境マップを設定
	auto* skinnedModelRenderer = renderManager->GetRenderer(RenderPassType::SkinnedModel);
	if (skinnedModelRenderer) {
		static_cast<SkinnedModelRenderer*>(skinnedModelRenderer)->SetEnvironmentMap(environmentMapTexture.gpuHandle);
	}


	// ===== 3Dゲームオブジェクトの生成と初期化=====
	// Sphereオブジェクト
	auto sphere = CreateObject<SphereObject>();
	sphere->Initialize();
	sphere->SetActive(false);  // Skeletonテスト中は非表示

	// Fenceオブジェクト
	auto fence = CreateObject<FenceObject>();
	fence->Initialize();
	fence->SetActive(false);  // Skeletonテスト中は非表示

	// Terrainオブジェクト
	auto terrain = CreateObject<TerrainObject>();
	terrain->Initialize();
	terrain->SetActive(false);  // Skeletonテスト中は非表示

	// AnimatedCubeオブジェクト
	auto animatedCube = CreateObject<AnimatedCubeObject>();
	animatedCube->Initialize();
	animatedCube->SetActive(false);  // Skeletonテスト中は非表示

	// SkeletonModelオブジェクト
	auto skeletonModel = CreateObject<SkeletonModelObject>();
	skeletonModel->Initialize();
	skeletonModel->SetActive(true);

	// WalkModelオブジェクト
	auto walkModel = CreateObject<WalkModelObject>();
	walkModel->Initialize();
	walkModel->SetActive(true);

	// SneakWalkModelオブジェクト
	auto sneakWalkModel = CreateObject<SneakWalkModelObject>();
	sneakWalkModel->Initialize();
	sneakWalkModel->SetActive(true);

	// SkyBoxの初期化
	auto skyBox = CreateObject<SkyBoxObject>();
	skyBox->Initialize();
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
}

void TestScene::Draw()
{
	BaseScene::Draw();
}


void TestScene::Finalize()
{
}
}
