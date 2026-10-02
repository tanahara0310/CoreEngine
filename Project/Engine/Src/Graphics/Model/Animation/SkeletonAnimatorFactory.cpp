#include "pch.h"
#include "SkeletonAnimatorFactory.h"
#include "Graphics/Model/Skeleton/SkeletonAnimator.h"

namespace CoreEngine
{
    std::unique_ptr<IAnimationController> SkeletonAnimatorFactory::CreateSkeletonAnimator(
        const Skeleton& skeleton,
        const Animation& animation,
        bool loop) const
    {
        return std::make_unique<SkeletonAnimator>(skeleton, animation, loop);
    }
}
