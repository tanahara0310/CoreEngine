#pragma once

#include "BossAction.h"
#include "Engine/Math/Vector/Vector3.h"

// 前方宣言
class BossObject;

/// @brief 待機アクション
class IdleAction : public BossAction {
public:
	IdleAction(BossObject* boss, float duration = 2.0f);
	
	void Start() override;
	void Update(float deltaTime) override;
	const char* GetName() const override { return "Idle"; }

private:
	BossObject* boss_;
	float duration_;
	float elapsedTime_ = 0.0f;
};

/// @brief 歩行アクション
class WalkAction : public BossAction {
public:
	WalkAction(BossObject* boss, float duration = 3.0f);
	
	void Start() override;
	void Update(float deltaTime) override;
	const char* GetName() const override { return "Walk"; }

private:
	BossObject* boss_;
	float duration_;
	float elapsedTime_ = 0.0f;
};

/// @brief ジャンプ攻撃アクション
class JumpAttackAction : public BossAction {
public:
	JumpAttackAction(BossObject* boss);
	
	void Start() override;
	void Update(float deltaTime) override;
	void End() override;
	const char* GetName() const override { return "JumpAttack"; }

private:
	BossObject* boss_;
	float duration_ = 2.0f;  // ボスのジャンプ攻撃アニメーション時間と同じ
	float cooldownDuration_ = 0.3f;  // 攻撃後のクールダウン時間（短縮）
	float elapsedTime_ = 0.0f;
	bool isInCooldown_ = false;  // クールダウン中フラグ
	bool hitboxSpawned_ = false;  // ヒットボックス生成済みフラグ
	CoreEngine::Vector3 targetPosition_;  // 攻撃開始時のターゲット位置
};

/// @brief パンチ攻撃アクション
class PunchAttackAction : public BossAction {
public:
	PunchAttackAction(BossObject* boss);
	
	void Start() override;
	void Update(float deltaTime) override;
	void End() override;
	const char* GetName() const override { return "PunchAttack"; }

private:
	BossObject* boss_;
	float duration_ = 1.0f;  // ボスのパンチアニメーション時間と同じ
	float cooldownDuration_ = 0.2f;  // 攻撃後のクールダウン時間（短縮）
	float elapsedTime_ = 0.0f;
	bool isInCooldown_ = false;  // クールダウン中フラグ
};

/// @brief 突進攻撃アクション
class DashAttackAction : public BossAction {
public:
	DashAttackAction(BossObject* boss);
	
	void Start() override;
	void Update(float deltaTime) override;
	void End() override;
	const char* GetName() const override { return "DashAttack"; }

private:
	BossObject* boss_;
	float chargeDuration_ = 0.3f;     // チャージ時間（短縮）
	float dashDuration_ = 0.8f;       // 突進時間
	float cooldownDuration_ = 0.3f;   // 攻撃後のクールダウン時間（短縮）
	float elapsedTime_ = 0.0f;
	
	enum class Phase {
		Charging,    // チャージ中
		Dashing,     // 突進中
		Cooldown     // クールダウン
	};
	
	Phase currentPhase_ = Phase::Charging;
	CoreEngine::Vector3 dashDirection_;  // 突進方向
	float dashSpeed_ = 15.0f;  // 突進速度（高速化）
};

/// @brief 円形弾幕攻撃アクション
class CircularBarrageAction : public BossAction {
public:
	CircularBarrageAction(BossObject* boss, int bulletCount = 12);
	
	void Start() override;
	void Update(float deltaTime) override;
	void End() override;
	const char* GetName() const override { return "CircularBarrage"; }

private:
	BossObject* boss_;
	float cooldownDuration_ = 0.3f;    // 攻撃後のクールダウン時間（短縮）
	float elapsedTime_ = 0.0f;
	bool hasFired_ = false;            // 弾を発射したかのフラグ
	int bulletCount_ = 12;             // 発射する弾の数
};
