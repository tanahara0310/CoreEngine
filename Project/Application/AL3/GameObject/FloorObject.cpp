#include "FloorObject.h"
#include "Engine/EngineSystem/EngineSystem.h"
#include "Engine/Graphics/Model/ModelManager.h"
#include "Engine/Graphics/Common/DirectXCommon.h"
#include "Engine/Graphics/TextureManager.h"
#include "Engine/Camera/ICamera.h"

void FloorObject::Initialize() {
	auto engine = GetEngineSystem();
	// 必須コンポーネントの取得
	auto dxCommon = engine->GetComponent<CoreEngine::DirectXCommon>();
	auto modelManager = engine->GetComponent<CoreEngine::ModelManager>();

	if (!dxCommon || !modelManager) {
		return;
	}

	// 静的モデルとして作成（床モデル）
	model_ = modelManager->CreateStaticModel("Assets/AppAssets/Floor/floor.obj");

	// トランスフォームの初期化
	transform_.Initialize(dxCommon->GetDevice());

	// 床の初期設定
	transform_.scale = { 1.0f, 1.0f, 1.0f };
	transform_.translate = { 0.0f, -5.0f, 0.0f };
	transform_.rotate = { 0.0f, 0.0f, 0.0f };

	// テクスチャの読み込み（床用のテクスチャがある場合）
	auto& textureManager = CoreEngine::TextureManager::GetInstance();
	texture_ = textureManager.Load("Assets/AppAssets/Floor/floor.png");

	// アクティブ状態に設定
	SetActive(true);
	transform_.TransferMatrix();
}

void FloorObject::Update() {
	if (!IsActive() || !model_) {
		return;
	}

	// トランスフォームの更新
	transform_.TransferMatrix();
}

void FloorObject::Draw(const CoreEngine::ICamera* camera) {
	if (!model_ || !camera) return;

	// モデルの描画
	model_->Draw(transform_, camera, texture_.gpuHandle);
}
