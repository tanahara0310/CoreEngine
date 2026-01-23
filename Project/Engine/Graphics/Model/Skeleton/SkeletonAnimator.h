#pragma once
#include "Skeleton.h"
#include "Engine/Graphics/Model/Animation/Animation.h"
#include "Engine/Graphics/Model/Animation/IAnimationController.h"

/// @brief スケルトンアニメーションコントローラー
/// スケルトン（ボーン）アニメーションを制御する

namespace CoreEngine
{
class SkeletonAnimator : public IAnimationController {
public:
	/// @brief コンストラクタ
	/// @param skeleton スケルトン（コピーして保持）
	/// @param animation アニメーション
	/// @param looping ループ再生するか（デフォルト: true）
	SkeletonAnimator(const Skeleton& skeleton, const Animation& animation, bool looping = true);

	/// @brief デストラクタ
	~SkeletonAnimator() override = default;

	/// @brief アニメーションを更新
	/// @param deltaTime デルタタイム（秒）
	void Update(float deltaTime) override;

	/// @brief アニメーション時刻を取得
	/// @return 現在の再生時刻（秒）
	float GetAnimationTime() const override { return animationTime_; }

	/// @brief アニメーションをリセット
	void Reset() override { animationTime_ = 0.0f; }

	/// @brief アニメーションが終了したか確認
	/// @return アニメーションが終了していればtrue
	bool IsFinished() const override;

	/// @brief ループ再生を設定
	/// @param loop ループ再生するか
	void SetLooping(bool loop) { isLooping_ = loop; }

	/// @brief ループ再生かどうかを取得
	/// @return ループ再生ならtrue
	bool IsLooping() const { return isLooping_; }
	
	/// @brief スケルトンを取得
	/// @return スケルトンの参照
	Skeleton& GetSkeleton() { return skeleton_; }
	const Skeleton& GetSkeleton() const { return skeleton_; }

private:
	/// @brief スケルトンにアニメーションを適用して行列を更新
	void ApplyAnimationAndUpdateMatrices();

	// スケルトン（コピーして保持）
	Skeleton skeleton_;
	
	// 再生中のアニメーション
	const Animation* animation_;
	
	// 現在の再生時刻（秒）
	float animationTime_;
	
	// ループ再生フラグ
	bool isLooping_;
};
}
