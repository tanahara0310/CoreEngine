#pragma once

#include "Animation.h"
#include "IAnimationController.h"
#include <Math/Matrix/Matrix4x4.h>
#include <string>

/// @brief キーフレームアニメーション再生クラス
/// ノード変形（SRT）のアニメーションを制御する

namespace CoreEngine
{
class Animator : public IAnimationController {
public:
	/// @brief デフォルトコンストラクタ
	Animator() = default;

	/// @brief コンストラクタ（アニメーション付き）
	/// @param animation 再生するアニメーション
	/// @param looping ループ再生するか（デフォルト: true）
	explicit Animator(const Animation& animation, bool looping = true);

	/// @brief デストラクタ
	~Animator() override = default;

	/// @brief アニメーションを設定
	/// @param animation 再生するアニメーション
	void SetAnimation(const Animation& animation);

	/// @brief ループ再生を設定
	/// @param loop ループ再生するか
	void SetLooping(bool loop) { isLooping_ = loop; }

	/// @brief ループ再生かどうかを取得
	/// @return ループ再生ならtrue
	bool IsLooping() const { return isLooping_; }

	/// @brief アニメーションが設定されているか確認
	/// @return アニメーションが設定されていればtrue
	bool HasAnimation() const { return animation_ != nullptr; }

	/// @brief 指定したノードのローカル変換行列を取得
	/// @param nodeName ノード名
	/// @return ローカル変換行列
	Matrix4x4 GetNodeLocalMatrix(const std::string& nodeName) const;

	// IAnimationController インターフェース
	void Update(float deltaTime) override;
	float GetAnimationTime() const override { return animationTime_; }
	void Reset() override { animationTime_ = 0.0f; }
	bool IsFinished() const override;

private:
	// 再生中のアニメーション
	const Animation* animation_ = nullptr;
	
	// 現在の再生時刻（秒）
	float animationTime_ = 0.0f;
	
	// ループ再生フラグ
	bool isLooping_ = true;
};
}
