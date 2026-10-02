#pragma once
#include "IAnimationControllerFactory.h"

namespace CoreEngine
{
    /// @brief IAnimationControllerFactory の標準実装
    /// SkeletonAnimator を生成する
    class SkeletonAnimatorFactory : public IAnimationControllerFactory {
    public:
        std::unique_ptr<IAnimationController> CreateSkeletonAnimator(
            const Skeleton& skeleton,
            const Animation& animation,
            bool loop) const override;
    };
}
