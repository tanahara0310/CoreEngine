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

		// HDRファイルを読み込み（自動的にキューブマップDDSに変換される）
		TextureManager::LoadedTexture environmentMapTexture;
		environmentMapTexture = textureManager.Load("Texture/kloppenheim_06_puresky_4k.hdr");


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

		// ===== LineManagerの使用例 =====
		// ImGuiの「ラインデバッグ」ウィンドウからも設定できますが、
		// シーン側から動的に描画することもできます

		auto& lineManager = LineManager::GetInstance();

		// 例1: 原点にクロスマーカーを表示
		lineManager.DrawCross(
			Vector3(0.0f, 0.0f, 0.0f),  // 位置
			0.3f,                        // サイズ
			Vector3(1.0f, 0.0f, 0.0f),  // 色（赤）
			1.0f                         // 透明度
		);

		// 例2: Spaceキーを押している間、デバッググリッドを表示
		if (keyboard->IsKeyPressed(DIK_SPACE)) {
			lineManager.DrawGrid(
				30.0f,                       // サイズ
				15,                          // 分割数
				Vector3(0.0f, 0.0f, 0.0f),  // 中心
				Vector3(0.0f, 0.8f, 1.0f),  // 色（シアン）
				0.8f                         // 透明度
			);
		}

		// 例3: 移動する球のデバッグ表示
		static float time = 0.0f;
		time += 0.016f; // 約60FPS想定

		Vector3 movingPosition = {
			std::sin(time) * 5.0f,
			2.0f,
			std::cos(time) * 5.0f
		};

		// 移動する位置にワイヤー球を表示
		lineManager.DrawWireSphere(
			movingPosition,
			1.0f,                        // 半径
			16,                          // セグメント数
			Vector3(1.0f, 1.0f, 0.0f),  // 色（黄色）
			1.0f                         // 透明度
		);

		// その位置から原点への線を引く
		lineManager.DrawLine(
			Vector3(0.0f, 2.0f, 0.0f),  // 始点（原点、高さ2）
			movingPosition,              // 終点（移動する位置）
			Vector3(1.0f, 0.5f, 0.0f),  // 色（オレンジ）
			0.5f                         // 透明度
		);

#ifdef _DEBUG
		// デバッグUIから設定したラインを描画（テストシーンのみ）
		lineManager.UpdateDebugDrawing();
#endif
	}

	void TestScene::Draw()
	{
		BaseScene::Draw();
	}


	void TestScene::Finalize()
	{
	}
}
