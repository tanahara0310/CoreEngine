#pragma once

#include "Engine/Math/Vector/Vector3.h"
#include "Engine/Math/Vector/Vector2.h"
#include "Engine/Collider/CollisionManager.h"
#include "Engine/Collider/CollisionConfig.h"
#include <list>
#include <memory>
#include <functional>

// 前方宣言
namespace CoreEngine {
	class EngineSystem;
	class ParticleSystem;
	class ICamera;
}
class PlayerObject;
class BulletObject;
class BossBulletObject;
class ReticleObject;
class BossObject;
class JumpAttackHitbox;

/// @brief ゲームプレイのメインロジックを管理するクラス
class GamePlayManager {
public:
	/// @brief 初期化
	/// @param engine エンジンシステム
	/// @param player プレイヤー
	/// @param reticle レティクル
	/// @param boss ボス
	void Initialize(
		CoreEngine::EngineSystem* engine,
		PlayerObject* player,
		ReticleObject* reticle,
		BossObject* boss
	);

	/// @brief 更新
	/// @param isOpeningPlaying オープニング演出中かどうか
	void Update(bool isOpeningPlaying);

	/// @brief 衝突判定システムの初期化
	void InitializeCollision();

	/// @brief パーティクルシステムの初期化
	/// @param createObjectCallback オブジェクト生成コールバック
	void InitializeParticles(std::function<CoreEngine::ParticleSystem*()> createParticleCallback);

	/// @brief 弾の生成コールバックを設定
	void SetSpawnBulletCallback(std::function<void()> callback);

	/// @brief ジャンプ攻撃ヒットボックスを追加
	void AddJumpAttackHitbox(JumpAttackHitbox* hitbox);

	/// @brief 弾を追加
	void AddBullet(BulletObject* bullet);

	/// @brief ボス弾を追加
	void AddBossBullet(BossBulletObject* bullet);

	/// @brief 衝突判定マネージャーを取得
	CoreEngine::CollisionManager* GetCollisionManager() const { return collisionManager_.get(); }

	/// @brief ボス撃破パーティクルが終了したかチェック
	/// @return パーティクルが終了している場合true
	bool IsBossDestroyParticleFinished() const;

private:
	/// @brief 着地パーティクルシステムの初期化
	void InitializeLandingParticle(CoreEngine::ParticleSystem* particle);

	/// @brief 弾ヒットパーティクルシステムの初期化
	void InitializeBulletHitParticle(CoreEngine::ParticleSystem* particle);

	/// @brief ボス撃破モデルパーティクルシステムの初期化
	void InitializeBossDestroyParticle(CoreEngine::ParticleSystem* particle);

	/// @brief 着地パーティクルを再生
	void PlayLandingParticle(const CoreEngine::Vector3& position);

	/// @brief 弾ヒットパーティクルを再生
	void PlayBulletHitParticle(const CoreEngine::Vector3& position);

	/// @brief ボス撃破パーティクルを再生
	void PlayBossDestroyParticle(const CoreEngine::Vector3& position);

	/// @brief プレイヤーの射撃処理
	void HandlePlayerShooting();

	/// @brief オブジェクトのクリーンアップ
	void CleanupDestroyedObjects();

	/// @brief 衝突判定の更新
	void UpdateCollisions();

	/// @brief プレイヤーのコライダーを登録
	void RegisterPlayerCollider();

	/// @brief ボスのコライダーを登録
	void RegisterBossCollider();

	/// @brief 弾のコライダーを登録
	void RegisterBulletColliders();

	/// @brief ボス弾のコライダーを登録
	void RegisterBossBulletColliders();

	/// @brief ジャンプ攻撃ヒットボックスのコライダーを登録
	void RegisterJumpAttackHitboxColliders();

	CoreEngine::EngineSystem* engine_ = nullptr;
	PlayerObject* player_ = nullptr;
	ReticleObject* reticle_ = nullptr;
	BossObject* boss_ = nullptr;

	// 弾のリスト
	std::list<BulletObject*> bullets_;

	// ボス弾のリスト
	std::list<BossBulletObject*> bossBullets_;

	// ジャンプ攻撃ヒットボックスのリスト
	std::list<JumpAttackHitbox*> jumpAttackHitboxes_;

	// パーティクルシステム（着地用）
	CoreEngine::ParticleSystem* landingParticle_ = nullptr;

	// パーティクルシステム（弾ヒット用）- プールとして複数保持
	std::vector<CoreEngine::ParticleSystem*> bulletHitParticlePool_;
	static constexpr size_t kBulletHitParticlePoolSize = 10;

	// パーティクルシステム（ボス撃破用モデルパーティクル）
	CoreEngine::ParticleSystem* bossDestroyParticle_ = nullptr;

	// 衝突判定システム
	std::unique_ptr<CoreEngine::CollisionConfig> collisionConfig_;
	std::unique_ptr<CoreEngine::CollisionManager> collisionManager_;

	// 弾生成コールバック
	std::function<void()> spawnBulletCallback_;
};
