#include "SkeletonAnimator.h"
#include "Engine/Graphics/Model/Animation/AnimationUtils.h"
#include "Engine/Math/MathCore.h"
#include <algorithm>
#include <cassert>


namespace CoreEngine
{
SkeletonAnimator::SkeletonAnimator(const Skeleton& skeleton, const Animation& animation, bool looping)
    : skeleton_(skeleton)
    , animation_(&animation)
    , animationTime_(0.0f)
    , isLooping_(looping) {
    assert(animation_);
}

void SkeletonAnimator::Update(float deltaTime) {
    // 時刻を進める
    animationTime_ += deltaTime;

    // ループ制御
    if (isLooping_) {
        animationTime_ = std::fmod(animationTime_, animation_->duration);
    } else {
        animationTime_ = std::min(animationTime_, animation_->duration);
    }

    // スケルトンにアニメーションを適用して行列を更新
    ApplyAnimationAndUpdateMatrices();
}

bool SkeletonAnimator::IsFinished() const {
    return !isLooping_ && animationTime_ >= animation_->duration;
}

void SkeletonAnimator::ApplyAnimationAndUpdateMatrices() {
    // すべてのJointに対してアニメーションを適用して行列を更新
    for (Joint& joint : skeleton_.joints) {
        // アニメーションデータがあれば適用
        auto it = animation_->nodeAnimations.find(joint.name);
        if (it != animation_->nodeAnimations.end()) {
            const NodeAnimation& nodeAnimation = it->second;
            
            // translate, rotate, scaleの値を計算
            joint.transform.translate = AnimationUtils::CalculateVector3(
                nodeAnimation.translate.keyframes, 
                animationTime_
            );
            joint.transform.rotate = AnimationUtils::CalculateQuaternion(
                nodeAnimation.rotate.keyframes, 
                animationTime_
            );
            joint.transform.scale = AnimationUtils::CalculateVector3(
                nodeAnimation.scale.keyframes, 
                animationTime_
            );
        }

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
                skeleton_.joints[*joint.parent].skeletonSpaceMatrix
            );
        } else {
            // 親がいないのでlocalMatrixとskeletonSpaceMatrixは一致する
            joint.skeletonSpaceMatrix = joint.localMatrix;
        }
    }
}
}
