#pragma once

#include "IAnimationController.h"
#include "Engine/Graphics/Model/Skeleton/Skeleton.h"
#include "Engine/Utility/Timer/GameTimer.h"
#include <memory>
#include <optional>

/// @brief 2つのアニメーションをブレンドするコントローラー

namespace CoreEngine
{
class AnimationBlender : public IAnimationController {
public:
	/// @brief コンストラクタ
	/// @param currentController 現在のアニメーションコントローラー
	AnimationBlender(std::unique_ptr<IAnimationController> currentController);

	/// @brief デストラクタ
	~AnimationBlender() override = default;

	/// @brief アニメーションを更新
	/// @param deltaTime デルタタイム（秒）
	void Update(float deltaTime) override;

	/// @brief アニメーション時刻を取得
	/// @return 現在の再生時刻（秒）
	float GetAnimationTime() const override;

	/// @brief アニメーションをリセット
	void Reset() override;

	/// @brief アニメーションが終了したか確認
	/// @return アニメーションが終了していればtrue
	bool IsFinished() const override;

	/// @brief 新しいアニメーションへのブレンドを開始
	/// @param targetController ターゲットアニメーションコントローラー
	/// @param blendDuration ブレンド時間（秒）
	void StartBlend(std::unique_ptr<IAnimationController> targetController, float blendDuration);

	/// @brief ブレンド中かどうかを取得
	/// @return ブレンド中ならtrue
	bool IsBlending() const { return blendTimer_.IsActive(); }

	/// @brief スケルトンを取得（SkeletonAnimator用）
	/// @return スケルトンの参照
	Skeleton& GetSkeleton();
	const Skeleton& GetSkeleton() const;

private:
	/// @brief 2つのスケルトンをブレンド
	/// @param skeleton1 スケルトン1
	/// @param skeleton2 スケルトン2
	/// @param weight スケルトン2の重み（0.0～1.0）
	/// @return ブレンドされたスケルトン
	Skeleton BlendSkeletons(const Skeleton& skeleton1, const Skeleton& skeleton2, float weight);

	/// @brief ブレンド完了時のコールバック
	void OnBlendComplete();

	// 現在のアニメーションコントローラー
	std::unique_ptr<IAnimationController> currentController_;
	
	// ターゲットアニメーションコントローラー（ブレンド中のみ）
	std::unique_ptr<IAnimationController> targetController_;
	
	// ブレンド用のスケルトン（結果を保持）
	std::optional<Skeleton> blendedSkeleton_;
	
	// ブレンドタイマー
	GameTimer blendTimer_;
};
}
