#include "AnimationBlender.h"
#include "Graphics/Model/Skeleton/SkeletonAnimator.h"
#include "Math/MathCore.h"
#include "Utility/Collision/CollisionUtils.h"
#include <algorithm>


namespace CoreEngine
{
AnimationBlender::AnimationBlender(std::unique_ptr<IAnimationController> currentController)
    : currentController_(std::move(currentController)) {
    blendTimer_.SetName("AnimationBlendTimer");
}

void AnimationBlender::Update(float deltaTime) {
    if (!currentController_) return;

    // 現在のアニメーションを更新
    currentController_->Update(deltaTime);

    // ブレンド中の処理
    if (blendTimer_.IsActive() && targetController_) {
        // タイマーを更新
        blendTimer_.Update(deltaTime);

        // ターゲットアニメーションも更新
        targetController_->Update(deltaTime);

        // ブレンドの重み計算（0.0～1.0）
        float blendWeight = blendTimer_.GetProgress();

        // スケルトンのブレンド
        auto* currentSkeletonAnimator = dynamic_cast<SkeletonAnimator*>(currentController_.get());
        auto* targetSkeletonAnimator = dynamic_cast<SkeletonAnimator*>(targetController_.get());

        if (currentSkeletonAnimator && targetSkeletonAnimator) {
            const Skeleton& skeleton1 = currentSkeletonAnimator->GetSkeleton();
            const Skeleton& skeleton2 = targetSkeletonAnimator->GetSkeleton();
            blendedSkeleton_ = BlendSkeletons(skeleton1, skeleton2, blendWeight);
        }

        // ブレンド完了チェック
        if (blendTimer_.IsFinished()) {
            OnBlendComplete();
        }
    }
}

float AnimationBlender::GetAnimationTime() const {
    if (currentController_) {
        return currentController_->GetAnimationTime();
    }
    return 0.0f;
}

void AnimationBlender::Reset() {
    if (currentController_) {
        currentController_->Reset();
    }
    if (targetController_) {
        targetController_->Reset();
    }
    blendTimer_.Reset();
}

bool AnimationBlender::IsFinished() const {
    if (currentController_) {
        return currentController_->IsFinished();
    }
    return true;
}

void AnimationBlender::StartBlend(std::unique_ptr<IAnimationController> targetController, float blendDuration) {
    if (!targetController) return;

    targetController_ = std::move(targetController);
    
    // ブレンドタイマーを開始
    blendTimer_.Start(blendDuration, false);
}

void AnimationBlender::OnBlendComplete() {
    // ターゲットアニメーションに完全に切り替え
    currentController_ = std::move(targetController_);
    targetController_.reset();
    blendedSkeleton_.reset();
}

Skeleton& AnimationBlender::GetSkeleton() {
    // ブレンド中はブレンドされたスケルトンを返す
    if (blendTimer_.IsActive() && blendedSkeleton_) {
        return *blendedSkeleton_;
    }

    // ブレンド中でない場合は現在のアニメーションのスケルトンを返す
    auto* skeletonAnimator = dynamic_cast<SkeletonAnimator*>(currentController_.get());
    if (skeletonAnimator) {
        return skeletonAnimator->GetSkeleton();
    }

    // フォールバック（空のスケルトンを作成）
    static Skeleton emptySkeleton;
    return emptySkeleton;
}

const Skeleton& AnimationBlender::GetSkeleton() const {
    // ブレンド中はブレンドされたスケルトンを返す
    if (blendTimer_.IsActive() && blendedSkeleton_) {
        return *blendedSkeleton_;
    }

    // ブレンド中でない場合は現在のアニメーションのスケルトンを返す
    auto* skeletonAnimator = dynamic_cast<SkeletonAnimator*>(currentController_.get());
    if (skeletonAnimator) {
        return skeletonAnimator->GetSkeleton();
    }

    // フォールバック（空のスケルトンを作成）
    static Skeleton emptySkeleton;
    return emptySkeleton;
}

Skeleton AnimationBlender::BlendSkeletons(const Skeleton& skeleton1, const Skeleton& skeleton2, float weight) {
    Skeleton result = skeleton1;

    // 各ジョイントをブレンド
    for (size_t i = 0; i < result.joints.size() && i < skeleton2.joints.size(); ++i) {
        Joint& joint = result.joints[i];
        const Joint& joint2 = skeleton2.joints[i];

        // 平行移動の線形補間
        joint.transform.translate = CollisionUtils::Lerp(
            joint.transform.translate,
            joint2.transform.translate,
            weight
        );

        // 回転のSlerp（球面線形補間）
        joint.transform.rotate = MathCore::QuaternionMath::Slerp(
            joint.transform.rotate,
            joint2.transform.rotate,
            weight
        );

        // スケールの線形補間
        joint.transform.scale = CollisionUtils::Lerp(
            joint.transform.scale,
            joint2.transform.scale,
            weight
        );

        // TransformからlocalMatrixを更新
        joint.localMatrix = MathCore::Matrix::MakeAffine(
            joint.transform.scale,
            joint.transform.rotate,
            joint.transform.translate
        );

        // 親がいれば親の行列を掛ける
        if (joint.parent) {
            joint.skeletonSpaceMatrix = MathCore::Matrix::Multiply(
                joint.localMatrix,
                result.joints[*joint.parent].skeletonSpaceMatrix
            );
        } else {
            joint.skeletonSpaceMatrix = joint.localMatrix;
        }
    }

    return result;
}
}
