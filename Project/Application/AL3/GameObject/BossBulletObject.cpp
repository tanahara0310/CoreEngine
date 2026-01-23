#include "BossBulletObject.h"
#include "Engine/EngineSystem/EngineSystem.h"
#include "Engine/Graphics/Common/DirectXCommon.h"
#include "Engine/Utility/FrameRate/FrameRateController.h"
#include "Engine/Camera/ICamera.h"
#include "Application/AL3/GameObject/PlayerObject.h"

void BossBulletObject::Initialize(std::unique_ptr<CoreEngine::Model> model, CoreEngine::TextureManager::LoadedTexture texture, const CoreEngine::Vector3& position, const CoreEngine::Vector3& direction, float speed) {
	auto engine = GetEngineSystem();

	// モデルとテクスチャを外部から受け取る
	model_ = std::move(model);
	texture_ = texture;

	// Transformの初期化
	auto dxCommon = engine->GetComponent<CoreEngine::DirectXCommon>();
	if (dxCommon) {
		transform_.Initialize(dxCommon->GetDevice());
	}

	// 初期位置を設定
	transform_.translate = position;
	transform_.scale = { 0.5f, 0.5f, 0.5f };  // 少し小さめに
	transform_.rotate = { 0.0f, 0.0f, 0.0f };

	// 速度を設定
	speed_ = speed;
	velocity_ = {
		direction.x * speed_,
		direction.y * speed_,
		direction.z * speed_
	};

	// アクティブ状態に設定
	SetActive(true);

	// 寿命タイマーを初期化・開始
	lifetimeTimer_.SetName("BossBulletLifetimeTimer");
	lifetimeTimer_.Start(lifetime_, false);

	// 寿命終了時に削除マークを設定
	lifetimeTimer_.SetOnComplete([this]() {
		Destroy();
	});

	// 球形コライダーを作成（半径: 0.5f）
	collider_ = std::make_unique<CoreEngine::SphereCollider>(this, 0.5f);
	collider_->SetLayer(CoreEngine::CollisionLayer::BossAttack);  // ボスの攻撃レイヤー
}

void BossBulletObject::Update() {
	if (!IsActive()) {
		return;
	}

	auto engine = GetEngineSystem();

	// FrameRateControllerから1フレームあたりの時間を取得
	auto frameRateController = engine->GetComponent<CoreEngine::FrameRateController>();
	if (!frameRateController) {
		return;
	}

	float deltaTime = frameRateController->GetDeltaTime();

	// 寿命タイマーの更新
	lifetimeTimer_.Update(deltaTime);

	// 位置を更新（速度ベクトルに基づいて移動）
	transform_.translate.x += velocity_.x * deltaTime;
	transform_.translate.y += velocity_.y * deltaTime;
	transform_.translate.z += velocity_.z * deltaTime;

	// 回転アニメーション（見た目のため）
	transform_.rotate.y += 5.0f * deltaTime;

	// Transformの更新
	transform_.TransferMatrix();

	// 範囲外チェック
	if (IsOutOfBounds()) {
		Destroy();
	}
}

void BossBulletObject::Draw(const CoreEngine::ICamera* camera) {
	if (!camera || !model_) return;

	// モデルの描画
	model_->Draw(transform_, camera, texture_.gpuHandle);
}

void BossBulletObject::OnCollisionEnter(CoreEngine::GameObject* other) {
	if (!other) return;

	// プレイヤーと衝突した場合
	if (auto* player = dynamic_cast<PlayerObject*>(other)) {
		// プレイヤーにダメージを与える
		player->TakeDamage(15);  // 15ダメージ
		Destroy();  // 弾を削除
	}
}

bool BossBulletObject::IsOutOfBounds() const {
	// XZ平面での距離をチェック（Y軸は除外）
	float distanceXZ = std::sqrt(
		transform_.translate.x * transform_.translate.x +
		transform_.translate.z * transform_.translate.z
	);

	// 範囲外チェック
	return distanceXZ > BOUNDS_LIMIT;
}
