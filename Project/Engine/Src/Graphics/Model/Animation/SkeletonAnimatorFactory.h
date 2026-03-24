#pragma once
#include "IAnimationControllerFactory.h"

namespace CoreEngine
{
    /// @brief IAnimationControllerFactory の標準実装
    /// SkeletonAnimator と AnimationBlender を生成する
    class SkeletonAnimatorFactory : public IAnimationControllerFactory {
    public:
        std::unique_ptr<IAnimationController> CreateSkeletonAnimator(
            const Skeleton& skeleton,
            const Animation& animation,
            bool loop) const override;

        std::unique_ptr<IAnimationController> CreateBlenderWithTarget(
            std::unique_ptr<IAnimationController> from,
            std::unique_ptr<IAnimationController> to,
            float blendDuration) const override;
    };
}
