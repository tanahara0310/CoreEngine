#include "BulletObject.h"
#include "Engine/EngineSystem/EngineSystem.h"
#include "Engine/Graphics/Model/ModelManager.h"
#include "Engine/Graphics/Common/DirectXCommon.h"
#include "Engine/Graphics/TextureManager.h"
#include "Engine/Camera/ICamera.h"
#include "Engine/Utility/FrameRate/FrameRateController.h"
#include "Engine/Math/MathCore.h"
#include "Engine/Collider/SphereCollider.h"
#include "Application/AL3/GameObject/BossObject.h"
#include <numbers>

void BulletObject::Initialize(std::unique_ptr<CoreEngine::Model> model, CoreEngine::TextureManager::LoadedTexture texture, const CoreEngine::Vector3& position, const CoreEngine::Vector3& direction) {
	auto engine = GetEngineSystem();

	// モデルとテクスチャを外部から受け取る
	model_ = std::move(model);
	texture_ = texture;

	// Transformの初期化
	auto dxCommon = engine->GetComponent<CoreEngine::DirectXCommon>();
	if (dxCommon) {
		transform_.Initialize(dxCommon->GetDevice());
	}

	// 初期位置・スケール設定
	transform_.translate = position;
	transform_.scale = { 1.0f, 1.0f, 1.0f };
	
	// 弾を横向きにするため、X軸で90度回転（π/2ラジアン）
	// さらに、発射方向に向けるためにY軸回転を計算
	float yaw = std::atan2f(direction.x, direction.z);
	transform_.rotate = { std::numbers::pi_v<float> / 2.0f, yaw, 0.0f };

	// 速度ベクトルを計算（方向を正規化して速度をかける）
	CoreEngine::Vector3 normalizedDirection = CoreEngine::MathCore::Vector::Normalize(direction);
	velocity_ = CoreEngine::MathCore::Vector::Multiply(speed_, normalizedDirection);

	// アクティブ状態に設定
	SetActive(true);

	// 寿命タイマーを開始
	lifetimeTimer_.SetName("BulletLifetimeTimer");
	lifetimeTimer_.Start(lifetime_, false);
	
	// 寿命が切れたら自動削除
	lifetimeTimer_.SetOnComplete([this]() {
		Destroy();
	});

	// コライダーを作成（半径0.3の球形 - より小さく調整）
	collider_ = std::make_unique<CoreEngine::SphereCollider>(this, 0.3f);
	collider_->SetLayer(CoreEngine::CollisionLayer::PlayerBullet);

	transform_.TransferMatrix();
}

void BulletObject::Update() {
	if (!IsActive() || !model_) {
		return;
	}

	auto engine = GetEngineSystem();
	auto frameRateController = engine->GetComponent<CoreEngine::FrameRateController>();
	if (!frameRateController) {
		return;
	}

	float deltaTime = frameRateController->GetDeltaTime();

	// 移動処理
	CoreEngine::Vector3 movement = CoreEngine::MathCore::Vector::Multiply(deltaTime, velocity_);
	transform_.translate = CoreEngine::MathCore::Vector::Add(transform_.translate, movement);

	// Transformの更新
	transform_.TransferMatrix();

	// 寿命タイマーの更新
	lifetimeTimer_.Update(deltaTime);
}

void BulletObject::Draw(const CoreEngine::ICamera* camera) {
	if (!camera || !model_) return;
	
	// モデルの描画
	model_->Draw(transform_, camera, texture_.gpuHandle);
}

void BulletObject::OnCollisionEnter(CoreEngine::GameObject* other) {
	if (!other) return;

	// ボスとの衝突をチェック
	if (auto* boss = dynamic_cast<BossObject*>(other)) {
		// ヒット時のコールバックを呼び出し（パーティクル生成）
		// 弾の位置ではなくボスの中心位置を渡す
		if (onHitCallback_) {
			CoreEngine::Vector3 bossCenter = boss->GetTransform().translate;
			onHitCallback_(bossCenter);
		}

		// 弾を削除マークする（GamePlayManagerで削除される）
		Destroy();
	}
}

bool BulletObject::IsOutOfBounds() const {
	// アリーナの範囲外に出たかチェック
	return (std::abs(transform_.translate.x) > BOUNDS_LIMIT ||
			std::abs(transform_.translate.z) > BOUNDS_LIMIT);
}
