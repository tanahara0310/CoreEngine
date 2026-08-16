#include "pch.h"
#include "SkeletonAnimator.h"
#include "Graphics/Model/Animation/AnimationUtils.h"
#include "Math/MathCore.h"
#include "Utility/Logger/Logger.h"
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

    // アニメーションはジョイント名で引くため、名前が一致しないと
    // 「エラーは出ないがバインドポーズのまま動かない」という無音の失敗になる。
    // 一致数を数えて、0 件なら警告を出す。
    size_t matchedJointCount = 0;
    for (const Joint& joint : skeleton_.joints) {
        if (animation_->nodeAnimations.contains(joint.name)) {
            ++matchedJointCount;
        }
    }

    Logger::GetInstance().Logf(
        matchedJointCount == 0 ? LogLevel::WARNING : LogLevel::INFO,
        LogCategory::Graphics,
        "SkeletonAnimator: joints={} nodeAnimations={} matched={} duration={:.3f}s",
        skeleton_.joints.size(), animation_->nodeAnimations.size(),
        matchedJointCount, animation_->duration);
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
            joint.skeletonSpaceMatrix = joint.localMatrix * skeleton_.joints[*joint.parent].skeletonSpaceMatrix;
        } else {
            // 親がいないのでlocalMatrixとskeletonSpaceMatrixは一致する
            joint.skeletonSpaceMatrix = joint.localMatrix;
        }
    }
}
}
