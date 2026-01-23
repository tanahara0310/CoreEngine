#include "AssignmentScene.h"
#include <EngineSystem.h>

#ifdef _DEBUG
#include "Engine/Camera/Debug/CameraDebugUI.h"
#endif

#include "WinApp/WinApp.h"
#include "Scene/SceneManager.h"
#include "Engine/Graphics/Render/RenderManager.h"
#include "Engine/Graphics/Render/Model/ModelRenderer.h"
#include "Engine/Graphics/TextureManager.h"
#include "Engine/Graphics/Light/LightManager.h"

using namespace CoreEngine::MathCore;

namespace CoreEngine
{
	void AssignmentScene::Initialize(EngineSystem* engine)
	{
		BaseScene::Initialize(engine);

		///========================================================
		// モデルの読み込みと初期化
		///========================================================

		// コンポーネントを直接取得
		auto dxCommon = engine_->GetComponent<DirectXCommon>();
		auto modelManager = engine_->GetComponent<ModelManager>();
		auto renderManager = engine_->GetComponent<RenderManager>();
		auto lightManager = engine_->GetComponent<LightManager>();

		if (!dxCommon || !modelManager || !renderManager || !lightManager) {
			return; // 必須コンポーネントがない場合は終了
		}

		// ===== 3Dゲームオブジェクトの生成と初期化 =====
		
		// Sphereオブジェクト
		sphere_ = CreateObject<SphereObject>();
		sphere_->Initialize();
		sphere_->SetActive(true);
		sphere_->GetTransform().translate = { 0.0f, 2.0f, 0.0f };
		sphere_->GetTransform().scale = { 2.0f, 2.0f, 2.0f };

		// Terrainオブジェクト
		terrain_ = CreateObject<TerrainObject>();
		terrain_->Initialize();
		terrain_->SetActive(true);
		terrain_->GetTransform().translate = { 0.0f, 0.0f, 0.0f };
		terrain_->GetTransform().scale = { 3.0f, 3.0f, 3.0f };

		// WalkModelオブジェクト1（モンスターボールの左側）
		walkModel1_ = CreateObject<WalkModelObject>();
		walkModel1_->Initialize();
		walkModel1_->SetActive(true);
		walkModel1_->GetTransform().translate = { -4.0f, 0.0f, 0.0f };
		walkModel1_->GetTransform().scale = { 1.0f, 1.0f, 1.0f };

		// WalkModelオブジェクト2（モンスターボールの右側）
		walkModel2_ = CreateObject<WalkModelObject>();
		walkModel2_->Initialize();
		walkModel2_->SetActive(true);
		walkModel2_->GetTransform().translate = { 4.0f, 0.0f, 0.0f };
		walkModel2_->GetTransform().scale = { 1.0f, 1.0f, 1.0f };

		// ===== ライトの設定 =====

		// ポイントライト1 - 左側（赤色）
		auto pointLight1 = lightManager->AddPointLight();
		if (pointLight1) {
			pointLight1->color = { 1.0f, 0.2f, 0.2f, 1.0f };
			pointLight1->position = { -5.0f, 3.0f, 0.0f };
			pointLight1->intensity = 1.0f;
			pointLight1->radius = 15.0f;
			pointLight1->decay = 1.0f;
			pointLight1->enabled = true;
		}

		// ポイントライト2 - 右側（青色）
		auto pointLight2 = lightManager->AddPointLight();
		if (pointLight2) {
			pointLight2->color = { 0.2f, 0.2f, 1.0f, 1.0f };
			pointLight2->position = { 5.0f, 3.0f, 0.0f };
			pointLight2->intensity = 1.0f;
			pointLight2->radius = 15.0f;
			pointLight2->decay = 1.0f;
			pointLight2->enabled = true;
		}

		// スポットライト1 - 前側（黄色）
		auto spotLight1 = lightManager->AddSpotLight();
		if (spotLight1) {
			spotLight1->color = { 1.0f, 1.0f, 0.2f, 1.0f };
			spotLight1->position = { 0.0f, 8.0f, -8.0f };
			spotLight1->direction = Vector::Normalize({ 0.0f, -1.0f, 1.0f });
			spotLight1->intensity = 2.0f;
			spotLight1->distance = 20.0f;
			spotLight1->decay = 1.0f;
			spotLight1->cosAngle = std::cos(30.0f * 3.14159f / 180.0f);  // 30度
			spotLight1->cosFalloffStart = std::cos(20.0f * 3.14159f / 180.0f);  // 20度
			spotLight1->enabled = true;
		}

		// スポットライト2 - 後側（緑色）
		auto spotLight2 = lightManager->AddSpotLight();
		if (spotLight2) {
			spotLight2->color = { 0.2f, 1.0f, 0.2f, 1.0f };
			spotLight2->position = { 0.0f, 8.0f, 8.0f };
			spotLight2->direction = Vector::Normalize({ 0.0f, -1.0f, -1.0f });
			spotLight2->intensity = 2.0f;
			spotLight2->distance = 20.0f;
			spotLight2->decay = 1.0f;
			spotLight2->cosAngle = std::cos(30.0f * 3.14159f / 180.0f);  // 30度
			spotLight2->cosFalloffStart = std::cos(20.0f * 3.14159f / 180.0f);  // 20度
			spotLight2->enabled = true;
		}

		// エリアライト - 上部中央（白色）
		auto areaLight = lightManager->AddAreaLight();
		if (areaLight) {
			areaLight->color = { 1.0f, 0.9f, 0.8f, 1.0f };  // 暖色系の白
			areaLight->position = { 0.0f, 6.0f, 0.0f };
			areaLight->direction = Vector::Normalize({ 0.0f, -1.0f, 0.0f });  // 下向き
			areaLight->intensity = 1.5f;
			areaLight->width = 3.0f;
			areaLight->height = 3.0f;
			areaLight->right = { 1.0f, 0.0f, 0.0f };
			areaLight->up = { 0.0f, 0.0f, 1.0f };
			areaLight->decay = 1.0f;
			areaLight->enabled = true;
		}

#ifdef _DEBUG
		auto console = engine_->GetConsole();
		if (console) {
			console->LogInfo("AssignmentScene: シーン初期化完了");
			console->LogInfo("AssignmentScene: ポイントライト x2、スポットライト x2、エリアライト x1 を設定");
		}
#endif
	}

	void AssignmentScene::OnUpdate()
	{
		// 特にシーン固有の更新処理は不要
		// ライトの制御はBaseSceneの「ライト制御」タブで行う
	}

	void AssignmentScene::Draw()
	{
		BaseScene::Draw();
	}

	void AssignmentScene::Finalize()
	{
#ifdef _DEBUG
		auto console = engine_->GetConsole();
		if (console) {
			console->LogInfo("AssignmentScene: シーン終了処理");
		}
#endif

		BaseScene::Finalize();
	}
}
