#include "JumpAttackHitbox.h"
#include "Engine/Collider/AABBCollider.h"
#include "Engine/EngineSystem/EngineSystem.h"
#include "Engine/Utility/FrameRate/FrameRateController.h"
#include "Application/AL3/GameObject/PlayerObject.h"

JumpAttackHitbox::JumpAttackHitbox() = default;

JumpAttackHitbox::~JumpAttackHitbox() = default;

void JumpAttackHitbox::Initialize(const CoreEngine::Vector3& position, const CoreEngine::Vector3& size, float activeDuration) {
	// 位置を設定
	transform_.translate = position;
	transform_.scale = { 1.0f, 1.0f, 1.0f };
	transform_.rotate = { 0.0f, 0.0f, 0.0f };

	// コライダーを作成
	collider_ = std::make_unique<CoreEngine::AABBCollider>(this, size);
	collider_->SetLayer(CoreEngine::CollisionLayer::BossAttack);

	// アクティブ状態に設定
	SetActive(true);

	// タイマーを設定（時間経過後に自動削除）
	activeTimer_.SetName("JumpAttackHitbox");
	activeTimer_.Start(activeDuration, false);
	activeTimer_.SetOnComplete([this]() {
		Destroy();  // タイマー終了時に自己削除
	});
}

void JumpAttackHitbox::Update() {
	if (!IsActive()) {
		return;
	}

	auto engine = GetEngineSystem();
	auto frameRateController = engine->GetComponent<CoreEngine::FrameRateController>();
	if (!frameRateController) {
		return;
	}

	float deltaTime = frameRateController->GetDeltaTime();

	// タイマー更新
	activeTimer_.Update(deltaTime);

	// Transform更新
	transform_.TransferMatrix();
}

void JumpAttackHitbox::OnCollisionEnter(CoreEngine::GameObject* other) {
	if (!other) return;

	// プレイヤーと衝突した場合
	if (auto* player = dynamic_cast<PlayerObject*>(other)) {
		// 無敵中の場合はダメージを与えない
		if (player->IsInvincible()) {
			return;
		}
		
		// プレイヤーにダメージを与える
		player->TakeDamage(20);  // 20ダメージ
		
		// ヒットしたら即座にヒットボックスを削除（1回のみヒット）
		Destroy();
	}
}
