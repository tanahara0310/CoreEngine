#include "Animator.h"
#include "AnimationUtils.h"
#include <Math/MathCore.h>
#include <algorithm>


using namespace CoreEngine::MathCore;

namespace CoreEngine
{
	Animator::Animator(const Animation& animation, bool looping)
		: animation_(&animation)
		, animationTime_(0.0f)
		, isLooping_(looping) {
	}

	void Animator::SetAnimation(const Animation& animation) {
		animation_ = &animation;
		animationTime_ = 0.0f;
	}

	void Animator::Update(float deltaTime) {
		if (!animation_) return;

		// 時刻を進める
		animationTime_ += deltaTime;

		// ループ制御
		if (isLooping_) {
			animationTime_ = std::fmod(animationTime_, animation_->duration);
		} else {
			animationTime_ = std::min(animationTime_, animation_->duration);
		}
	}

	bool Animator::IsFinished() const {
		if (!animation_) return true;
		return !isLooping_ && animationTime_ >= animation_->duration;
	}

	Matrix4x4 Animator::GetNodeLocalMatrix(const std::string& nodeName) const {
		// アニメーションが設定されていない、またはノードアニメーションがない場合は単位行列を返す
		if (!animation_) {
			return Matrix::Identity();
		}

		auto it = animation_->nodeAnimations.find(nodeName);
		if (it == animation_->nodeAnimations.end()) {
			return Matrix::Identity();
		}

		const NodeAnimation& nodeAnimation = it->second;

		// 各チャンネル（translate, rotate, scale）の値を取得
		Vector3 translate = AnimationUtils::CalculateVector3(nodeAnimation.translate.keyframes, animationTime_);
		Quaternion rotate = AnimationUtils::CalculateQuaternion(nodeAnimation.rotate.keyframes, animationTime_);
		Vector3 scale = AnimationUtils::CalculateVector3(nodeAnimation.scale.keyframes, animationTime_);

		// アフィン変換行列を生成（S * R * T）
		return Matrix::Multiply(
			Matrix::Multiply(Matrix::Scale(scale), QuaternionMath::MakeRotateMatrix(rotate)),
			Matrix::Translation(translate)
		);
	}
}
