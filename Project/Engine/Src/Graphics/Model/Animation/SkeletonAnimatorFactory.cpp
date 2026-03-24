#include "SkeletonAnimatorFactory.h"
#include "Graphics/Model/Skeleton/SkeletonAnimator.h"
#include "AnimationBlender.h"

namespace CoreEngine
{
    std::unique_ptr<IAnimationController> SkeletonAnimatorFactory::CreateSkeletonAnimator(
        const Skeleton& skeleton,
        const Animation& animation,
        bool loop) const
    {
        return std::make_unique<SkeletonAnimator>(skeleton, animation, loop);
    }

    std::unique_ptr<IAnimationController> SkeletonAnimatorFactory::CreateBlenderWithTarget(
        std::unique_ptr<IAnimationController> from,
        std::unique_ptr<IAnimationController> to,
        float blendDuration) const
    {
        auto blender = std::make_unique<AnimationBlender>(std::move(from));
        blender->StartBlend(std::move(to), blendDuration);
        return blender;
    }
}
