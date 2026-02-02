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
		environmentMapTexture = textureManager.Load("Texture/rostock_laage_airport_4k.dds");

		// ===== IBL Irradiance Mapの生成 =====
		auto* iblGenerator = engine_->GetComponent<IBLGenerator>();
		D3D12_GPU_DESCRIPTOR_HANDLE irradianceMapSRVHandle = {};
		D3D12_GPU_DESCRIPTOR_HANDLE prefilteredMapSRVHandle = {};
		D3D12_GPU_DESCRIPTOR_HANDLE brdfLUTSRVHandle = {};

		if (iblGenerator) {
			// Irradiance Mapを生成（128x128x6面）← より高解像度に
			irradianceMap_ = iblGenerator->GenerateIrradianceMap(
				environmentMapTexture.texture.Get(),
				128);

			// 生成成功した場合、SRVを作成
			if (irradianceMap_) {
				Logger::GetInstance().Log("Irradiance Map generated successfully for scene",
					LogLevel::INFO, LogCategory::Graphics);

				// SRVを作成
				D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
				auto* descriptorManager = dxCommon->GetDescriptorManager();

				D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
				srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
				srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
				srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				srvDesc.TextureCube.MipLevels = 1;
				srvDesc.TextureCube.MostDetailedMip = 0;

				descriptorManager->CreateSRV(
					irradianceMap_.Get(),
					srvDesc,
					cpuHandle,
					irradianceMapSRVHandle,
					"IBL_IrradianceMap");

				// DirectXCommonにIrradiance Mapを登録
				dxCommon->SetIrradianceMap(irradianceMap_.Get(), irradianceMapSRVHandle);

				Logger::GetInstance().Log("Irradiance Map SRV created successfully",
					LogLevel::INFO, LogCategory::Graphics);
			}

			// Prefiltered Environment Mapを生成（256x256、5ミップレベル）
			prefilteredMap_ = iblGenerator->GeneratePrefilteredEnvironmentMap(
				environmentMapTexture.texture.Get(),
				256);

			if (prefilteredMap_) {
				Logger::GetInstance().Log("Prefiltered Environment Map generated successfully",
					LogLevel::INFO, LogCategory::Graphics);

				// SRVを作成（全ミップレベル）
				D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
				auto* descriptorManager = dxCommon->GetDescriptorManager();

				D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
				srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
				srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
				srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				srvDesc.TextureCube.MostDetailedMip = 0;
				srvDesc.TextureCube.MipLevels = 5; // 5ミップレベル
				srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;

				descriptorManager->CreateSRV(
					prefilteredMap_.Get(),
					srvDesc,
					cpuHandle,
					prefilteredMapSRVHandle,
					"IBL_PrefilteredMap");

				// DirectXCommonにPrefiltered Mapを登録
				dxCommon->SetPrefilteredMap(prefilteredMap_.Get(), prefilteredMapSRVHandle);

				Logger::GetInstance().Log("Prefiltered Map SRV created successfully",
					LogLevel::INFO, LogCategory::Graphics);
			}

			// BRDF LUTを生成（512x512）
			brdfLUT_ = iblGenerator->GenerateBRDFLUT(512);

			if (brdfLUT_) {
				Logger::GetInstance().Log("BRDF LUT generated successfully",
					LogLevel::INFO, LogCategory::Graphics);

				// SRVを作成
				D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
				auto* descriptorManager = dxCommon->GetDescriptorManager();

				D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
				srvDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
				srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
				srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				srvDesc.Texture2D.MostDetailedMip = 0;
				srvDesc.Texture2D.MipLevels = 1;

				descriptorManager->CreateSRV(
					brdfLUT_.Get(),
					srvDesc,
					cpuHandle,
					brdfLUTSRVHandle,
					"IBL_BRDF_LUT");

				// DirectXCommonにBRDF LUTを登録
				dxCommon->SetBRDFLUT(brdfLUT_.Get(), brdfLUTSRVHandle);

				Logger::GetInstance().Log("BRDF LUT SRV created successfully",
					LogLevel::INFO, LogCategory::Graphics);
			}
		}

		// SkinnedModelRendererに環境マップとIBLリソースを設定
		auto* skinnedModelRenderer = renderManager->GetRenderer(RenderPassType::SkinnedModel);
		if (skinnedModelRenderer) {
			static_cast<SkinnedModelRenderer*>(skinnedModelRenderer)->SetEnvironmentMap(environmentMapTexture.gpuHandle);

			// IBL Irradiance Mapを設定
			if (irradianceMapSRVHandle.ptr != 0) {
				static_cast<SkinnedModelRenderer*>(skinnedModelRenderer)->SetIrradianceMap(irradianceMapSRVHandle);
			}
			// Prefiltered Mapを設定
			if (prefilteredMapSRVHandle.ptr != 0) {
				static_cast<SkinnedModelRenderer*>(skinnedModelRenderer)->SetPrefilteredMap(prefilteredMapSRVHandle);
			}
			// BRDF LUTを設定
			if (brdfLUTSRVHandle.ptr != 0) {
				static_cast<SkinnedModelRenderer*>(skinnedModelRenderer)->SetBRDFLUT(brdfLUTSRVHandle);
			}
		}

		//ModelRendererに環境マップを設定
		auto* modelRenderer = renderManager->GetRenderer(RenderPassType::Model);
		if (modelRenderer) {
			static_cast<ModelRenderer*>(modelRenderer)->SetEnvironmentMap(environmentMapTexture.gpuHandle);

			// IBL Irradiance Mapを設定
			if (irradianceMapSRVHandle.ptr != 0) {
				static_cast<ModelRenderer*>(modelRenderer)->SetIrradianceMap(irradianceMapSRVHandle);
			}
			// Prefiltered Mapを設定
			if (prefilteredMapSRVHandle.ptr != 0) {
				static_cast<ModelRenderer*>(modelRenderer)->SetPrefilteredMap(prefilteredMapSRVHandle);
			}
			// BRDF LUTを設定
			if (brdfLUTSRVHandle.ptr != 0) {
				static_cast<ModelRenderer*>(modelRenderer)->SetBRDFLUT(brdfLUTSRVHandle);
			}
		}


		// ===== 3Dゲームオブジェクトの生成と初期化=====
		// PBRテスト用に5x5グリッドで球体を配置
		// 横軸(X): Metallic (0.0 → 1.0)
		// 縦軸(Y): Roughness (0.0 → 1.0)

		const int gridSizeX = 5;  // Metallic軸
		const int gridSizeY = 5;  // Roughness軸
		const float spacingX = 3.0f;  // 横間隔
		const float spacingY = 3.0f;  // 縦間隔

		for (int row = 0; row < gridSizeY; ++row) {
			for (int col = 0; col < gridSizeX; ++col) {
				auto sphere = CreateObject<SphereObject>();
				sphere->Initialize();
				sphere->SetPBREnabled(true);

				// マテリアルカラーを設定（グレーでPBRテスト）
				sphere->SetMaterialColor({ 0.5f, 0.5f, 0.5f, 1.0f });

				// 環境マップを有効化
				sphere->SetEnvironmentMapEnabled(true);
				sphere->SetEnvironmentMapIntensity(1.0f);

				// IBLを有効化
				sphere->SetIBLEnabled(true);
				sphere->SetIBLIntensity(1.0f);

				// Metallic: 左(0.0) → 右(1.0)
				float metallic = static_cast<float>(col) / static_cast<float>(gridSizeX - 1);

				// Roughness: 下(0.0) → 上(1.0)
				float roughness = static_cast<float>(row) / static_cast<float>(gridSizeY - 1);

				sphere->SetPBRParameters(metallic, roughness, 1.0f);

				// XY平面上にグリッド配置（中央を原点に、Z=0）
				float xPos = (col - gridSizeX / 2.0f) * spacingX;
				float yPos = (row - gridSizeY / 2.0f) * spacingY;
				sphere->GetTransform().translate = { xPos, yPos, 0.0f };
				sphere->SetActive(true);
			}
		}

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
		skeletonModel->SetActive(false);  // PBRテスト中は非表示

		// WalkModelオブジェクト
		auto walkModel = CreateObject<WalkModelObject>();
		walkModel->Initialize();
		walkModel->SetActive(false);  // PBRテスト中は非表示

		// SneakWalkModelオブジェクト
		auto sneakWalkModel = CreateObject<SneakWalkModelObject>();
		sneakWalkModel->Initialize();
		sneakWalkModel->SetActive(false);  // PBRテスト中は非表示

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
