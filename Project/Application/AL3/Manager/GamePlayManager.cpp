#include "GamePlayManager.h"
#include "Engine/EngineSystem/EngineSystem.h"
#include "Engine/Utility/FrameRate/FrameRateController.h"
#include "Engine/Graphics/Common/DirectXCommon.h"
#include "Engine/Graphics/Resource/ResourceFactory.h"
#include "Engine/Graphics/Model/ModelManager.h"
#include "Engine/Collider/CollisionManager.h"
#include "Engine/Collider/CollisionConfig.h"
#include "Engine/Collider/Collider.h"
#include "Engine/Collider/AABBCollider.h"
#include "Engine/Collider/SphereCollider.h"
#include "Engine/Particle/ParticleSystem.h"
#include "Application/AL3/GameObject/PlayerObject.h"
#include "Application/AL3/GameObject/BulletObject.h"
#include "Application/AL3/GameObject/BossBulletObject.h"
#include "Application/AL3/GameObject/ReticleObject.h"
#include "Application/AL3/GameObject/BossObject.h"
#include "Application/AL3/GameObject/JumpAttackHitbox.h"

using CoreEngine::CollisionConfig;
using CoreEngine::CollisionManager;
using CoreEngine::CollisionLayer;
using CoreEngine::BlendMode;
using CoreEngine::ModelManager;
using CoreEngine::Collider;

void GamePlayManager::Initialize(
	CoreEngine::EngineSystem* engine,
	PlayerObject* player,
	ReticleObject* reticle,
	BossObject* boss
) {

	engine_ = engine;
	player_ = player;
	reticle_ = reticle;
	boss_ = boss;
}

void GamePlayManager::InitializeCollision() {
	// 衝突判定設定を作成
	collisionConfig_ = std::make_unique<CollisionConfig>();

	// プレイヤーの弾とボスの衝突を有効化
	collisionConfig_->SetCollisionEnabled(CollisionLayer::PlayerBullet, CollisionLayer::Boss, true);

	// プレイヤーとボスの衝突を有効化
	collisionConfig_->SetCollisionEnabled(CollisionLayer::Player, CollisionLayer::Boss, true);

	// プレイヤーとボス攻撃の衝突を有効化
	collisionConfig_->SetCollisionEnabled(CollisionLayer::Player, CollisionLayer::BossAttack, true);

	// 衝突判定マネージャーを作成
	collisionManager_ = std::make_unique<CollisionManager>(collisionConfig_.get());
}

void GamePlayManager::InitializeParticles(std::function<CoreEngine::ParticleSystem* ()> createParticleCallback) {
	// DirectXCommonとResourceFactoryを取得
	auto dxCommon = engine_->GetComponent<CoreEngine::DirectXCommon>();
	auto resourceFactory = engine_->GetComponent<CoreEngine::ResourceFactory>();
	if (!dxCommon || !resourceFactory || !createParticleCallback) {
		return;
	}

	// 着地パーティクルシステムを生成
	landingParticle_ = createParticleCallback();
	landingParticle_->Initialize(dxCommon, resourceFactory, "BossLandingParticle");
	InitializeLandingParticle(landingParticle_);

	// 弾ヒットパーティクルプールの初期化
	for (size_t i = 0; i < kBulletHitParticlePoolSize; ++i) {
		auto bulletHitParticle = createParticleCallback();
		bulletHitParticle->Initialize(dxCommon, resourceFactory, "BulletHitParticle_" + std::to_string(i));
		InitializeBulletHitParticle(bulletHitParticle);
		bulletHitParticlePool_.push_back(bulletHitParticle);
	}

	// ボス撃破モデルパーティクルシステムを生成
	bossDestroyParticle_ = createParticleCallback();
	bossDestroyParticle_->Initialize(dxCommon, resourceFactory, "BossDestroyParticle");
	InitializeBossDestroyParticle(bossDestroyParticle_);

	// ボスに着地パーティクルコールバックを設定
	if (boss_) {
		boss_->SetLandingParticleCallback([this](const CoreEngine::Vector3& position) {
			PlayLandingParticle(position);
			});

		// ボスに撃破パーティクルコールバックを設定
		boss_->SetDestroyParticleCallback([this](const CoreEngine::Vector3& position) {
			PlayBossDestroyParticle(position);
			});
	}
}

void GamePlayManager::InitializeLandingParticle(CoreEngine::ParticleSystem* particle) {
	if (!particle) return;

	// ModelManagerを取得してボクセルモデルリソースを設定
	auto modelManager = engine_->GetComponent<ModelManager>();
	if (!modelManager) return;

	// ボクセルモデルリソースを取得
	auto* voxelModelResource = modelManager->GetModelResource("Assets/AppAssets/Voxel/Voxel.obj");
	if (voxelModelResource) {
		particle->SetModelResource(voxelModelResource);
	}

	particle->SetBlendMode(BlendMode::kBlendModeNormal);
	particle->SetTexture("Texture/white1x1.png");

	// エミッションモジュールの設定
	{
		auto& emissionModule = particle->GetEmissionModule();
		auto emissionData = emissionModule.GetEmissionData();
		emissionData.rateOverTime = 0;
		emissionData.burstCount = 50;
		emissionData.burstTime = 0.0f;
		emissionModule.SetEmissionData(emissionData);
	}

	// 形状モジュールの設定
	{
		auto& shapeModule = particle->GetShapeModule();
		auto shapeData = shapeModule.GetShapeData();
		shapeData.shapeType = CoreEngine::ShapeModule::ShapeType::Circle;
		shapeData.radius = 1.0f;
		shapeData.emitFromSurface = true;
		shapeModule.SetShapeData(shapeData);
	}

	// 速度モジュールの設定
	{
		auto& velocityModule = particle->GetVelocityModule();
		auto velocityData = velocityModule.GetVelocityData();
		velocityData.startSpeed = { 0.0f, 3.0f, 0.0f };
		velocityData.randomSpeedRange = { 2.0f, 1.0f, 2.0f };
		velocityData.useRandomDirection = true;
		velocityModule.SetVelocityData(velocityData);
	}

	// MainModuleの設定（ダンジョンの石が削れたような色）
	{
		auto& mainModule = particle->GetMainModule();
		auto& mainData = mainModule.GetMainData();
		mainData.startLifetime = 1.5f;
		mainData.startLifetimeRandomness = 0.3f;
		mainData.startColor = { 0.6f, 0.55f, 0.5f, 1.0f };  // 古びた石の色（グレーベージュ）
		mainData.startSize = { 3.0f, 3.0f, 3.0f };
		mainData.startSizeRandomness = 0.5f;
		mainData.looping = false;
		mainData.duration = 0.1f;
		mainData.gravityModifier = 0.5f;
	}

	// 色モジュールの設定（石の破片が暗くフェード）
	{
		auto& colorModule = particle->GetColorModule();
		auto colorData = colorModule.GetColorData();
		colorData.useGradient = true;
		colorData.endColor = { 0.35f, 0.3f, 0.25f, 0.0f };  // ダークブラウングレーにフェード
		colorModule.SetColorData(colorData);
	}

	// サイズモジュールの設定
	{
		auto& sizeModule = particle->GetSizeModule();
		auto sizeData = sizeModule.GetSizeData();
		sizeData.endSize = 0.2f;
		sizeData.sizeOverLifetime = true;
		sizeModule.SetSizeData(sizeData);
	}

	// 回転モジュールの設定（3D回転）
	{
		auto& rotationModule = particle->GetRotationModule();
		auto rotationData = rotationModule.GetRotationData();
		rotationData.use2DRotation = false;
		rotationData.rotationSpeed = { 3.0f, 3.0f, 3.0f };
		rotationData.rotationSpeedRandomness = { 3.0f, 3.0f, 3.0f };
		rotationModule.SetRotationData(rotationData);
	}

	particle->SetActive(true);
}

void GamePlayManager::InitializeBulletHitParticle(CoreEngine::ParticleSystem* particle) {
	if (!particle) return;

	// ModelManagerを取得してボクセルモデルリソースを設定
	auto modelManager = engine_->GetComponent<ModelManager>();
	if (!modelManager) return;

	// ボクセルモデルリソースを取得
	auto* voxelModelResource = modelManager->GetModelResource("Assets/AppAssets/Voxel/Voxel.obj");
	if (voxelModelResource) {
		particle->SetModelResource(voxelModelResource);
	}

	particle->SetBlendMode(BlendMode::kBlendModeNormal);
	particle->SetTexture("Texture/white1x1.png");

	// エミッションモジュールの設定
	{
		auto& emissionModule = particle->GetEmissionModule();
		auto emissionData = emissionModule.GetEmissionData();
		emissionData.rateOverTime = 0;
		emissionData.burstCount = 10;
		emissionData.burstTime = 0.0f;
		emissionModule.SetEmissionData(emissionData);
	}

	// 形状モジュールの設定
	{
		auto& shapeModule = particle->GetShapeModule();
		auto shapeData = shapeModule.GetShapeData();
		shapeData.shapeType = CoreEngine::ShapeModule::ShapeType::Sphere;
		shapeData.radius = 0.1f;
		shapeData.emitFromSurface = false;
		shapeModule.SetShapeData(shapeData);
	}

	// 速度モジュールの設定
	{
		auto& velocityModule = particle->GetVelocityModule();
		auto velocityData = velocityModule.GetVelocityData();
		velocityData.startSpeed = { 0.0f, 1.5f, 0.0f };
		velocityData.randomSpeedRange = { 1.0f, 0.5f, 1.0f };
		velocityData.useRandomDirection = true;
		velocityModule.SetVelocityData(velocityData);
	}

	// MainModuleの設定（肌色が混じった緑 - 遺跡の守護神風）
	{
		auto& mainModule = particle->GetMainModule();
		auto& mainData = mainModule.GetMainData();
		mainData.startLifetime = 0.8f;
		mainData.startLifetimeRandomness = 0.2f;
		mainData.startColor = { 0.65f, 0.75f, 0.55f, 1.0f };  // 肌色がかった苔緑（守護神の神秘的な色）
		mainData.startSize = { 1.5f, 1.5f, 1.5f };
		mainData.startSizeRandomness = 0.2f;
		mainData.looping = false;
		mainData.duration = 0.1f;
		mainData.gravityModifier = 0.5f;
	}

	// 色モジュールの設定（守護神のエネルギーが消えていく）
	{
		auto& colorModule = particle->GetColorModule();
		auto colorData = colorModule.GetColorData();
		colorData.useGradient = true;
		colorData.endColor = { 0.4f, 0.5f, 0.35f, 0.0f };  // 深い緑にフェードアウト
		colorModule.SetColorData(colorData);
	}

	// サイズモジュールの設定
	{
		auto& sizeModule = particle->GetSizeModule();
		auto sizeData = sizeModule.GetSizeData();
		sizeData.endSize = 0.1f;
		sizeData.sizeOverLifetime = true;
		sizeModule.SetSizeData(sizeData);
	}

	// 回転モジュールの設定（3D回転）
	{
		auto& rotationModule = particle->GetRotationModule();
		auto rotationData = rotationModule.GetRotationData();
		rotationData.use2DRotation = false;
		rotationData.rotationSpeed = { 5.0f, 5.0f, 5.0f };
		rotationData.rotationSpeedRandomness = { 4.0f, 4.0f, 4.0f };
		rotationModule.SetRotationData(rotationData);
	}

	particle->SetActive(true);
}

void GamePlayManager::InitializeBossDestroyParticle(CoreEngine::ParticleSystem* particle) {
	if (!particle) return;

	// ModelManagerを取得してボスモデルリソースを設定
	auto modelManager = engine_->GetComponent<ModelManager>();
	if (!modelManager) return;

	// ボスのモデルリソースを取得
	auto* bossModelResource = modelManager->GetModelResource("Assets/AppAssets/Boss/BossModelParticle/bossModelParticle.obj");
	if (bossModelResource) {
		particle->SetModelResource(bossModelResource);
	}

	particle->SetBlendMode(BlendMode::kBlendModeNormal);
	particle->SetTexture("Assets/AppAssets/Boss/BossModelParticle/bossModelParticle.png");

	// エミッションモジュールの設定（大量バースト放出）
	{
		auto& emissionModule = particle->GetEmissionModule();
		auto emissionData = emissionModule.GetEmissionData();
		emissionData.rateOverTime = 0;
		emissionData.burstCount = 60;  // 30個→60個に増加して更に派手に
		emissionData.burstTime = 0.0f;
		emissionModule.SetEmissionData(emissionData);
	}

	// 形状モジュールの設定（球状に放出）
	{
		auto& shapeModule = particle->GetShapeModule();
		auto shapeData = shapeModule.GetShapeData();
		shapeData.shapeType = CoreEngine::ShapeModule::ShapeType::Sphere;
		shapeData.radius = 1.0f;  // 0.5f→1.0fに拡大
		shapeData.emitFromSurface = true;
		shapeModule.SetShapeData(shapeData);
	}

	// 速度モジュールの設定（上方向に強く爆発）
	{
		auto& velocityModule = particle->GetVelocityModule();
		auto velocityData = velocityModule.GetVelocityData();
		velocityData.startSpeed = { 0.0f, 20.0f, 0.0f };  // 上方向に大幅強化（5.0f→8.0f）
		velocityData.randomSpeedRange = { 4.0f, 2.0f, 4.0f };  // 横方向を抑えて上方向を優先
		velocityData.useRandomDirection = true;
		velocityModule.SetVelocityData(velocityData);
	}

	// MainModuleの設定
	{
		auto& mainModule = particle->GetMainModule();
		auto& mainData = mainModule.GetMainData();
		mainData.startLifetime = 3.0f;  // 2.0f→3.0fに延長
		mainData.startLifetimeRandomness = 0.8f;  // 0.5f→0.8fに増加
		mainData.startColor = { 1.0f, 1.0f, 1.0f, 1.0f };  // 白
		mainData.startSize = { 0.25f, 0.25f, 0.25f };  // 0.4f→0.25fに縮小
		mainData.startSizeRandomness = 0.5f;  // 0.3f→0.5fに増加してバラツキを強化
		mainData.startSpeed = 20.0f;  // 初速を更に強化（8.0f→10.0f）
		mainData.looping = false;
		mainData.duration = 0.1f;
		mainData.gravityModifier = 1.0f;  // 1.5f→2.0fに強化して重力を強める
	}

	// 色モジュールの設定（爆発的なグラデーション）
	{
		auto& colorModule = particle->GetColorModule();
		auto colorData = colorModule.GetColorData();
		colorData.useGradient = true;
		colorData.endColor = { 0.3f, 0.3f, 0.3f, 0.0f };  // 暗くフェードアウト
		colorModule.SetColorData(colorData);
	}

	// サイズモジュールの設定（徐々に小さく）
	{
		auto& sizeModule = particle->GetSizeModule();
		auto sizeData = sizeModule.GetSizeData();
		sizeData.endSize = 0.02f;  // 0.05f→0.02fにして完全に消えるように
		sizeData.sizeOverLifetime = true;
		sizeModule.SetSizeData(sizeData);
	}

	// 回転モジュールの設定（激しく回転しながら散らばる）
	{
		auto& rotationModule = particle->GetRotationModule();
		auto rotationData = rotationModule.GetRotationData();
		rotationData.use2DRotation = false;  // 3D回転
		rotationData.rotationSpeed = { 4.0f, 4.0f, 4.0f };  // 2.0f→4.0fに強化
		rotationData.rotationSpeedRandomness = { 5.0f, 5.0f, 5.0f };  // 3.0f→5.0fに強化
		rotationModule.SetRotationData(rotationData);
	}

	particle->SetActive(true);
}

void GamePlayManager::Update(bool isOpeningPlaying) {
	if (!player_ || !boss_) return;

	// オープニング演出中はゲームロジックを実行しない
	if (isOpeningPlaying) {
		return;
	}

	// プレイヤーの射撃処理
	HandlePlayerShooting();

	// オブジェクトのクリーンアップ
	CleanupDestroyedObjects();

	// 衝突判定の更新
	UpdateCollisions();
}

void GamePlayManager::HandlePlayerShooting() {
	if (!player_) return;

	// プレイヤーが弾を撃つか確認
	if (player_->ShouldShoot()) {
		// 弾発射アニメーションを再生
		player_->PlayShootAnimation();

		// 弾生成コールバックを実行
		if (spawnBulletCallback_) {
			spawnBulletCallback_();
		}

		// 弾発射後のクールダウンを開始
		player_->StartShootCooldown();
	}
}

void GamePlayManager::CleanupDestroyedObjects() {
	// 削除マークされた弾または範囲外の弾をリストから削除
	bullets_.remove_if([](BulletObject* bullet) {
		if (bullet == nullptr || bullet->IsMarkedForDestroy()) {
			return true;
		}

		// 範囲外チェック
		if (bullet->IsOutOfBounds()) {
			bullet->Destroy();  // 範囲外に出た弾を削除
			return true;
		}

		return false;
		});

	// 削除マークされたボス弾または範囲外のボス弾をリストから削除
	bossBullets_.remove_if([](BossBulletObject* bullet) {
		if (bullet == nullptr || bullet->IsMarkedForDestroy()) {
			return true;
		}

		// 範囲外チェック
		if (bullet->IsOutOfBounds()) {
			bullet->Destroy();
			return true;
		}

		return false;
		});

	// 削除マークされたジャンプ攻撃ヒットボックスまたはnullptrをリストから削除
	jumpAttackHitboxes_.remove_if([](JumpAttackHitbox* hitbox) {
		return hitbox == nullptr || hitbox->IsMarkedForDestroy();
		});
}

void GamePlayManager::UpdateCollisions() {
	if (!collisionManager_) return;

	// 衝突判定をクリアして再登録
	collisionManager_->Clear();

	// 各コライダーを登録
	RegisterPlayerCollider();
	RegisterBossCollider();
	RegisterBulletColliders();
	RegisterBossBulletColliders();
	RegisterJumpAttackHitboxColliders();

	// 衝突判定を実行
	collisionManager_->CheckAllCollisions();
}

void GamePlayManager::RegisterPlayerCollider() {
	if (player_ && player_->GetCollider()) {
		collisionManager_->RegisterCollider(static_cast<Collider*>(player_->GetCollider()));
	}
}

void GamePlayManager::RegisterBossCollider() {
	if (boss_ && boss_->GetCollider()) {
		collisionManager_->RegisterCollider(static_cast<Collider*>(boss_->GetCollider()));
	}
}

void GamePlayManager::RegisterBulletColliders() {
	for (auto* bullet : bullets_) {
		// nullptrチェックとコライダーの有効性確認を強化
		if (bullet && !bullet->IsMarkedForDestroy() && bullet->GetCollider()) {
			collisionManager_->RegisterCollider(static_cast<Collider*>(bullet->GetCollider()));
		}
	}
}

void GamePlayManager::RegisterBossBulletColliders() {
	for (auto* bullet : bossBullets_) {
		// nullptrチェックとコライダーの有効性確認
		if (bullet && !bullet->IsMarkedForDestroy() && bullet->GetCollider()) {
			collisionManager_->RegisterCollider(static_cast<Collider*>(bullet->GetCollider()));
		}
	}
}

void GamePlayManager::RegisterJumpAttackHitboxColliders() {
	for (auto* hitbox : jumpAttackHitboxes_) {
		// nullptrチェックとコライダーの有効性確認を強化
		if (hitbox && !hitbox->IsMarkedForDestroy() && hitbox->GetCollider()) {
			collisionManager_->RegisterCollider(static_cast<Collider*>(hitbox->GetCollider()));
		}
	}
}

void GamePlayManager::SetSpawnBulletCallback(std::function<void()> callback) {
	spawnBulletCallback_ = callback;
}

void GamePlayManager::AddJumpAttackHitbox(JumpAttackHitbox* hitbox) {
	jumpAttackHitboxes_.push_back(hitbox);
}

void GamePlayManager::AddBullet(BulletObject* bullet) {
	// ヒット時のコールバックを設定
	bullet->SetOnHitCallback([this](const CoreEngine::Vector3& hitPosition) {
		PlayBulletHitParticle(hitPosition);
		});

	bullets_.push_back(bullet);
}

void GamePlayManager::AddBossBullet(BossBulletObject* bullet) {
	bossBullets_.push_back(bullet);
}

void GamePlayManager::PlayLandingParticle(const CoreEngine::Vector3& position) {
	if (!landingParticle_) {
		return;
	}

	landingParticle_->SetEmitterPosition(position);
	landingParticle_->Clear();
	landingParticle_->Stop();

	auto& mainModule = landingParticle_->GetMainModule();
	mainModule.Restart();

	auto& emissionModule = landingParticle_->GetEmissionModule();
	emissionModule.Play();
}

void GamePlayManager::PlayBulletHitParticle(const CoreEngine::Vector3& position) {
	if (bulletHitParticlePool_.empty()) {
		return;
	}

	CoreEngine::ParticleSystem* bulletHitParticle = nullptr;
	for (auto& particle : bulletHitParticlePool_) {
		if (particle->IsFinished()) {
			bulletHitParticle = particle;
			break;
		}
	}

	if (!bulletHitParticle && !bulletHitParticlePool_.empty()) {
		bulletHitParticle = bulletHitParticlePool_[0];
	}

	if (bulletHitParticle) {
		bulletHitParticle->SetEmitterPosition(position);
		bulletHitParticle->Clear();
		bulletHitParticle->Stop();

		auto& mainModule = bulletHitParticle->GetMainModule();
		mainModule.Restart();

		auto& emissionModule = bulletHitParticle->GetEmissionModule();
		emissionModule.Play();
	}
}

void GamePlayManager::PlayBossDestroyParticle(const CoreEngine::Vector3& position) {
	if (!bossDestroyParticle_) {
		return;
	}

	bossDestroyParticle_->SetEmitterPosition(position);
	bossDestroyParticle_->Clear();
	bossDestroyParticle_->Stop();

	auto& mainModule = bossDestroyParticle_->GetMainModule();
	mainModule.Restart();

	auto& emissionModule = bossDestroyParticle_->GetEmissionModule();
	emissionModule.Play();
}

bool GamePlayManager::IsBossDestroyParticleFinished() const {
	return bossDestroyParticle_ && bossDestroyParticle_->IsFinished();
}
