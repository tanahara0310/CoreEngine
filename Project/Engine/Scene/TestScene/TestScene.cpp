#include <EngineSystem.h>

#ifdef _DEBUG
#include "Engine/Camera/Debug/CameraDebugUI.h"
#endif

#include "TestScene.h"
#include "WinApp/WinApp.h"
#include "Scene/SceneManager.h"

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

	if (!dxCommon || !modelManager) {
		return; // 必須コンポーネントがない場合は終了
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

	// SkyBoxの初期化（新システムに追加）
	auto skyBox = CreateObject<SkyBoxObject>();
	skyBox->Initialize();
	skyBox->SetActive(false);

	// スプライトオブジェクトの初期化（複数作成）
	// 画面中央を(0,0)とする座標系に変更
	auto sprite1 = CreateObject<SpriteObject>();
	sprite1->Initialize("Texture/uvChecker.png");
	sprite1->GetSpriteTransform().translate = { -200.0f, 100.0f, 0.0f };  // 左上付近
	sprite1->GetSpriteTransform().scale = { 0.5f, 0.5f, 1.0f };
	sprite1->SetActive(false);

	// スプライト2: circle（画面中央）
	auto sprite2 = CreateObject<SpriteObject>();
	sprite2->Initialize("Texture/circle.png");
	sprite2->GetSpriteTransform().translate = { 0.0f, 0.0f, 0.0f };  // 画面中央
	sprite2->GetSpriteTransform().scale = { 1.0f, 1.0f, 1.0f };
	sprite2->SetActive(false);

	// スプライト3: 別のuvChecker（右下）
	auto sprite3 = CreateObject<SpriteObject>();
	sprite3->Initialize("Texture/uvChecker.png");
	sprite3->GetSpriteTransform().translate = { 200.0f, -100.0f, 0.0f };  // 右下付近
	sprite3->GetSpriteTransform().scale = { 0.8f, 0.8f, 1.0f };
	sprite3->SetActive(false);

	// ===== パーティクルシステムの初期化 =====
	auto particleSystem = CreateObject<ParticleSystem>();
	particleSystem->Initialize(dxCommon, engine_->GetComponent<ResourceFactory>(), "BilbordParticle");

	// パーティクルシステムの設定
	particleSystem->SetEmitterPosition({ 0.0f, 2.0f, 0.0f });
	particleSystem->SetBlendMode(BlendMode::kBlendModeAdd);  // 加算合成
	particleSystem->SetBillboardType(BillboardType::ViewFacing);
	particleSystem_ = particleSystem;

	// エミッションモジュールの設定
	{
		auto& emissionModule = particleSystem_->GetEmissionModule();
		auto emissionData = emissionModule.GetEmissionData();
		emissionData.rateOverTime = 20;  // 1秒に20個のパーティクルを放出
		emissionModule.SetEmissionData(emissionData);
	}

	// 形状モジュールの設定
	{
		auto& shapeModule = particleSystem_->GetShapeModule();
		auto shapeData = shapeModule.GetShapeData();
		shapeData.shapeType = ShapeModule::ShapeType::Sphere;
		shapeData.radius = 0.5f;
		shapeData.emitFromSurface = false;
		shapeModule.SetShapeData(shapeData);
	}

	// 速度モジュールの設定
	{
		auto& velocityModule = particleSystem_->GetVelocityModule();
		auto velocityData = velocityModule.GetVelocityData();
		velocityData.startSpeed = { 0.0f, 1.0f, 0.0f };
		velocityData.randomSpeedRange = { 1.0f, 1.0f, 1.0f };
		velocityData.useRandomDirection = true;
		velocityModule.SetVelocityData(velocityData);
	}

	// MainModuleの設定
	{
		auto& mainModule = particleSystem_->GetMainModule();
		auto& mainData = mainModule.GetMainData();
		mainData.startLifetime = 2.0f;
		mainData.startLifetimeRandomness = 0.25f;
		mainData.startColor = { 1.0f, 0.8f, 0.2f, 1.0f };  // 黄色
		mainData.startSize = { 0.3f, 0.3f, 0.3f };
	}

	// 色モジュールの設定（グラデーションのみ）
	{
		auto& colorModule = particleSystem_->GetColorModule();
		auto colorData = colorModule.GetColorData();
		colorData.useGradient = true;
		colorData.endColor = { 1.0f, 0.2f, 0.0f, 0.0f };    // 赤でフェードアウト
		colorModule.SetColorData(colorData);
	}

	// サイズモジュールの設定（Over Lifetimeのみ）
	{
		auto& sizeModule = particleSystem_->GetSizeModule();
		auto sizeData = sizeModule.GetSizeData();
		sizeData.endSize = 0.05f;
		sizeData.sizeOverLifetime = true;
		sizeModule.SetSizeData(sizeData);
	}

	// パーティクルシステムを再生開始
	particleSystem_->Play();

	// ===== モデルパーティクルシステムの初期化 =====
	auto modelParticleSystem = CreateObject<ParticleSystem>();
	modelParticleSystem->Initialize(dxCommon, engine_->GetComponent<ResourceFactory>(), "modelParticle");

	// sphereモデルを読み込む
	sphereModelForParticle_ = modelManager->CreateStaticModel("SampleAssets/Sphere/sphere.obj");

	// ModelResourceを取得してParticleSystemに設定
	auto* sphereModelResource = modelManager->GetModelResource("SampleAssets/Sphere/sphere.obj");
	if (sphereModelResource) {
		modelParticleSystem->SetModelResource(sphereModelResource);
	}

	// モデルパーティクルのテクスチャを設定（オプション）
	modelParticleSystem->SetTexture("Texture/uvChecker.png");

	// モデルパーティクルシステムの設定
	modelParticleSystem->SetEmitterPosition({ 3.0f, 2.0f, 0.0f });
	modelParticleSystem->SetBlendMode(BlendMode::kBlendModeNormal);  // 通常合成
	modelParticleSystem_ = modelParticleSystem;

	// エミッションモジュールの設定（モデルパーティクル用）
	{
		auto& emissionModule = modelParticleSystem_->GetEmissionModule();
		auto emissionData = emissionModule.GetEmissionData();
		emissionData.rateOverTime = 10;  // 1秒に10個のパーティクルを放出
		emissionModule.SetEmissionData(emissionData);
	}

	// 形状モジュールの設定（モデルパーティクル用）
	{
		auto& shapeModule = modelParticleSystem_->GetShapeModule();
		auto shapeData = shapeModule.GetShapeData();
		shapeData.shapeType = ShapeModule::ShapeType::Box;
		shapeData.scale = { 1.0f, 0.1f, 1.0f };  // 箱型の放出形状
		shapeData.emitFromSurface = false;
		shapeModule.SetShapeData(shapeData);
	}

	// 速度モジュールの設定（モデルパーティクル用）
	{
		auto& velocityModule = modelParticleSystem_->GetVelocityModule();
		auto velocityData = velocityModule.GetVelocityData();
		velocityData.startSpeed = { 0.0f, 2.0f, 0.0f };  // 上方向に放出
		velocityData.randomSpeedRange = { 0.5f, 0.5f, 0.5f };
		velocityData.useRandomDirection = false;
		velocityModule.SetVelocityData(velocityData);
	}

	// MainModuleの設定（モデルパーティクル用）
	{
		auto& mainModule = modelParticleSystem_->GetMainModule();
		auto& mainData = mainModule.GetMainData();
		mainData.startLifetime = 3.0f;
		mainData.startLifetimeRandomness = 0.5f;
		mainData.startColor = { 0.2f, 0.8f, 1.0f, 1.0f };  // 水色
		mainData.startSize = { 0.2f, 0.2f, 0.2f };
	}

	// 色モジュールの設定（グラデーションのみ）
	{
		auto& colorModule = modelParticleSystem_->GetColorModule();
		auto colorData = colorModule.GetColorData();
		colorData.useGradient = true;
		colorData.endColor = { 1.0f, 1.0f, 1.0f, 0.0f };  // 白でフェードアウト
		colorModule.SetColorData(colorData);
	}

	// サイズモジュールの設定（モデルパーティクル用Over Lifetimeのみ）
	{
		auto& sizeModule = modelParticleSystem_->GetSizeModule();
		auto sizeData = sizeModule.GetSizeData();
		sizeData.endSize = 0.05f;
		sizeData.sizeOverLifetime = true;
		sizeModule.SetSizeData(sizeData);
	}

	// 回転モジュールの設定（モデルパーティクル用）
	{
		auto& rotationModule = modelParticleSystem_->GetRotationModule();
		auto rotationData = rotationModule.GetRotationData();
		rotationData.use2DRotation = false;  // 3D回転を使用
		rotationData.rotationSpeed = { 0.0f, 3.0f, 0.0f };// Y軸回転
		rotationData.rotationSpeedRandomness = { 1.0f, 1.0f, 1.0f };
		rotationModule.SetRotationData(rotationData);
	}

	// モデルパーティクルシステムを再生開始
	modelParticleSystem_->Play();

	// ===== TextObjectのテスト =====
	// タイトルテキスト
	auto titleText = CreateObject<TextObject>();
	titleText->Initialize("C:/Windows/Fonts/arial.ttf", 48, "TitleText");
	titleText->SetText("CoreEngine Font System");
	titleText->GetTransform().translate = { 640.0f, 50.0f, 0.0f };
	titleText->SetColor({ 1.0f, 0.8f, 0.0f, 1.0f });  // ゴールド
	titleText->SetActive(true);

	// FPS表示テキスト
	auto fpsText = CreateObject<TextObject>();
	fpsText->Initialize("C:/Windows/Fonts/arial.ttf", 32, "FPSText");
	fpsText->SetText("FPS: 60");
	fpsText->GetTransform().translate = { 50.0f, 50.0f, 0.0f };
	fpsText->SetColor({ 0.0f, 1.0f, 0.0f, 1.0f });  // 緑
	fpsText->SetActive(true);

	// 説明テキスト
	auto instructionText = CreateObject<TextObject>();
	instructionText->Initialize("C:/Windows/Fonts/arial.ttf", 24, "InstructionText");
	instructionText->SetText("Press SPACE to play/stop music\nPress TAB to restart scene");
	instructionText->GetTransform().translate = { 400.0f, 650.0f, 0.0f };
	instructionText->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });  // 白
	instructionText->SetActive(true);

	// 小さいテキスト
	auto smallText = CreateObject<TextObject>();
	smallText->Initialize("C:/Windows/Fonts/arial.ttf", 16, "SmallText");
	smallText->SetText("Small text test - 0123456789");
	smallText->GetTransform().translate = { 1000.0f, 700.0f, 0.0f };
	smallText->SetColor({ 0.7f, 0.7f, 0.7f, 1.0f });  // グレー
	smallText->SetActive(true);

	// テクスチャの読み込み

	auto& textureManager = TextureManager::GetInstance();
	textureChecker_ = textureManager.Load("Texture/uvChecker.png");
	textureCircle_ = textureManager.Load("Texture/circle.png");

	// サウンドリソースを取得
	auto soundManager = engine_->GetComponent<SoundManager>();
	if (soundManager) {
		mp3Resource_ = soundManager->CreateSoundResource("SampleAssets/Audio/BGM/test.mp3");
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

	// ===== スペースキーでMP3再生　====
	if (keyboard->IsKeyTriggered(DIK_SPACE)) {
		if (mp3Resource_ && mp3Resource_->IsValid()) {
			bool isPlaying = mp3Resource_->IsPlaying();
			if (isPlaying) {
				mp3Resource_->Stop();
			} else {
				mp3Resource_->Play(false);
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
