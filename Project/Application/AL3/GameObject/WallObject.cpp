#include "WallObject.h"
#include "Engine/EngineSystem/EngineSystem.h"
#include "Engine/Graphics/Model/ModelManager.h"
#include "Engine/Graphics/Common/DirectXCommon.h"
#include "Engine/Graphics/TextureManager.h"
#include "Engine/Camera/ICamera.h"

void WallObject::Initialize() {
	// デフォルトの位置・回転・スケールで初期化
	Initialize({ 0.0f, 0.0f, 0.0f }, 0.0f, { 1.0f, 1.0f, 1.0f });
}

void WallObject::Initialize(const CoreEngine::Vector3& position, float rotationY, const CoreEngine::Vector3& scale) {
	auto engine = GetEngineSystem();
	// 必須コンポーネントの取得
	auto dxCommon = engine->GetComponent<CoreEngine::DirectXCommon>();
	auto modelManager = engine->GetComponent<CoreEngine::ModelManager>();

	if (!dxCommon || !modelManager) {
		return;
	}

	// 静的モデルとして作成（壁モデル）
	model_ = modelManager->CreateStaticModel("Assets/AppAssets/Wall/wall.obj");

	// トランスフォームの初期化
	transform_.Initialize(dxCommon->GetDevice());

	// 壁の設定（引数から設定）
	transform_.scale = scale;
	transform_.translate = position;
	transform_.rotate = { 0.0f, rotationY, 0.0f };

	// テクスチャの読み込み（壁用のテクスチャがある場合）
	auto& textureManager = CoreEngine::TextureManager::GetInstance();
	texture_ = textureManager.Load("Assets/AppAssets/Wall/wall.png");

	// アクティブ状態に設定
	SetActive(true);
	transform_.TransferMatrix();
}

void WallObject::Update() {
	if (!IsActive() || !model_) {
		return;
	}

	// トランスフォームの更新
	transform_.TransferMatrix();
}

void WallObject::Draw(const CoreEngine::ICamera* camera) {
	if (!model_ || !camera) return;

	// モデルの描画
	model_->Draw(transform_, camera, texture_.gpuHandle);
}
