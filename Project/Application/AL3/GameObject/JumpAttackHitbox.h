#pragma once

#include "Engine/ObjectCommon/GameObject.h"
#include "Engine/Utility/Timer/GameTimer.h"
#include <memory>

// 前方宣言
namespace CoreEngine {
	class AABBCollider;
}

/// @brief ジャンプ攻撃の着地判定用一時的なヒットボックス
class JumpAttackHitbox : public CoreEngine::GameObject {
public:
	JumpAttackHitbox();
	~JumpAttackHitbox() override;

	/// @brief 初期化
	/// @param position 着地予定位置
	/// @param size ヒットボックスのサイズ
	/// @param activeDuration アクティブである時間（秒）
	void Initialize(const CoreEngine::Vector3& position, const CoreEngine::Vector3& size, float activeDuration);

	/// @brief 更新
	void Update() override;

	/// @brief 衝突開始イベント
	void OnCollisionEnter(CoreEngine::GameObject* other) override;

	/// @brief コライダーを取得
	CoreEngine::AABBCollider* GetCollider() const { return collider_.get(); }

private:
	std::unique_ptr<CoreEngine::AABBCollider> collider_;
	CoreEngine::GameTimer activeTimer_;
};
